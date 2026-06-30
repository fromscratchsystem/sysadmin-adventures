# Sysadmin Adventures

A terminal game written in C where the player manages a fictional datacenter set in 2031. The interface is entirely in ncurses; each "machine" is a Podman container accessible via SSH.

---

## Overall Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  main.c  — main loop + 7 command handlers                    │
├───────────┬─────────────┬────────────┬──────────┬────────────┤
│  ui.c     │  shell.c    │container.c │ infra.c  │ history.c  │
│ ncurses   │  SSH/PTY    │  Podman    │ physical │ command    │
│ layout    │  libssh2    │  CLI       │ datacenter│ history   │
├───────────┴─────────────┴────────────┴──────────┴────────────┤
│  vterm.c — custom VT100 emulator (ANSI parser + scrollback) │
└──────────────────────────────────────────────────────────────┘
```

### Data Flow

```
Keyboard → main.c → shell_send_line() → libssh2 → Container PTY
                                                       │
Screen ← vterm_render() ← vterm_process() ← libssh2 ←──┘
```

---

## Modules

### `defs.h` — Shared Types and Constants

Included by all other files. Defines:

| Symbol | Value | Role |
|---|---|---|
| `STATUS_HEIGHT` | 1 | Status bar height |
| `NARRATOR_RATIO` | 0.35 | Screen space reserved for narrator |
| `HISTORY_MAX` | 64 | Command history ring-buffer size |
| `CMD_MAX` | 256 | Max command length |
| `MAX_SHELLS` | 8 | Max simultaneous containers |
| `COL_*` | 1-7 | ncurses color pairs reserved for UI |

**Main structures:**

- `Panel` — ncurses window with border + content area (`border` / `inner`).
- `Layout` — complete set of windows: `narrator`, `tab_bar`, `shell`, `status`, `input_win`.
- `Shell` — SSH connection state: TCP socket, libssh2 session/channel, `VTerm` emulator, activity flag, additional networks.
- `History` — circular ring-buffer of `HISTORY_MAX` entries with navigation cursor.

---

### `main.c` — Entry Point and Main Loop

**Initialization (order matters)**

1. `container_init_network()` — creates Podman network before ncurses (`system()` calls write to stderr).
2. `container_ensure_running()` — builds/starts main container.
3. `infra_load()` + recreate Podman networks for switches (idempotent).
4. `sigaction(SIGWINCH, …)` — register resize handler.
5. `init_ncurses()` + `create_layout()` — build UI.
6. `attach_shell()` — open first SSH connection.
7. `state_load()` — reconnect to containers from previous session.

**Main Loop (select-based)**

Each iteration:
1. Handle pending SIGWINCH (rebuild layout, resize PTY).
2. `select()` on stdin + all active SSH sockets (100 ms timeout).
3. Read SSH output for each active shell → `shell_read_output()`.
4. Process keyboard input.

**Command Handler Structure**

Game commands (`/…`) are delegated to seven static functions. `main()` does linear dispatch:

```c
cmd_network()   // /network create|delete
cmd_deploy()    // /deploy
cmd_stop()      // /stop
cmd_rack()      // /rack create|list|show|delete
cmd_server()    // /server add|poweron|poweroff|list|delete
cmd_switch()    // /switch add|poweron|poweroff|list|delete
cmd_cable()     // /cable connect|disconnect|list
```

Each handler receives a `ShCtx *` (pointers to `shells`, `nshells`, `active`, `running`, `layout`) for commands that manipulate shells.

**Persistence**

- `~/.sysadmin-game.state` — list of deployed containers (restored at startup).
- `~/.sysadmin-game.infra` — physical infrastructure state (racks, servers, switches, cables).

---

### `ui.c` / `ui.h` — ncurses Interface

```
┌─ NARRATOR ──────────────────────────────────────┐  ← Panel (35% screen)
│ > Well. You exist. Congratulations.             │
└──────────────────────────────────────────────────┘
[F1 player@datacenter]  [F2 web01!]                   ← tab_bar (1 line)
┌─ TERMINAL ───────────────────────────────────────┐  ← Panel (remainder)
│                                                  │
└──────────────────────────────────────────────────┘
$ █                                                   ← input_win
 2031-03-14 03:47        SLA: 99.2%      Tickets: 3   ← status
```

**Main Functions:**

- `init_ncurses()` — locale, colors, 7 color pairs (narrator green, terminal cyan, etc.).
- `create_layout()` / `destroy_layout()` / `handle_resize()` — layout construction and rebuild. `handle_resize()` destroys old layout, recreates everything, resizes all active PTYs.
- `draw_tabs()` — displays `[Fn name]` for each shell; bold + `!` if unfocused activity.
- `draw_status()` — date, SLA, tickets in inverse green bar.
- `narrator_say(p, msg)` — pushes line into narrator ring-buffer and refreshes if live.
- `narrator_printf(p, fmt, …)` — shortcut for `vsnprintf` + `narrator_say` (avoids `char msg[N]; snprintf` outside ui.c).
- `narrator_scroll(p, delta)` — moves narrator view; `delta > 0` = scroll up.
- `redraw_input()` — redraws prompt `$ ` then input buffer.

**Narrator History**

Narrator panel content is stored in a static 512-line × 256-char ring-buffer in `ui.c`. This buffer survives resizes (unlike ncurses windows which are destroyed/recreated).

---

### `shell.c` / `shell.h` — SSH Connection via libssh2

**`shell_spawn(rows, cols, name, port)`**

1. `socket()` + `connect()` to `127.0.0.1:port`.
2. `libssh2_session_init()` + `libssh2_session_handshake()`.
3. `libssh2_userauth_password()` (user `player`, password `datacenter2031`).
4. Open SSH channel + request PTY `xterm-256color`.
5. Switch to **non-blocking** mode for `select()` integration.
6. Allocate `VTerm` with `first_pair = 8` (pairs 1-7 reserved for UI).

**`shell_send_line(sh, cmd)`** — Writes `cmd + "\n"` to channel (handles `LIBSSH2_ERROR_EAGAIN`).

**`shell_read_output(sh, win)`** — Reads up to 4096 bytes, loops until `EAGAIN` or EOF. Feeds data to `vterm_process()`. If `win != NULL`, calls `vterm_render()`.

**`shell_resize(sh, rows, cols)`** — `libssh2_channel_request_pty_size()` + `vterm_resize()`.

**`shell_close(sh)`** — Closes channel, session, socket and frees VTerm. Podman container remains active.

---

### `container.c` / `container.h` — Podman Container Management

All Podman commands are launched via `system()` redirected to `/dev/null` (`run_silent()`).

| Function | Description |
|---|---|
| `container_init_network()` | Creates `sysadmin-net` if missing |
| `container_ensure_running()` | Builds image if missing (~30 s), creates/starts main container on port 2222, waits for SSH |
| `container_deploy(name, image, port, nets, nnets)` | Deploys container, connects to additional networks, waits for SSH |
| `container_network_create(name)` | Creates Podman network (idempotent) |
| `container_network_delete(name)` | Deletes Podman network |
| `container_stop(name)` | `podman stop` without deletion |
| `container_is_running(name)` | Tests if container is running |

**`wait_for_ssh(port, SSH_WAIT_S)`** — Probes TCP port every second, up to `SSH_WAIT_S = 15` attempts.

The `podman run` command (with capabilities) is centralized in private helper `container_start_new()` to avoid duplication between `container_ensure_running()` and `container_deploy()`.

**Important Constants:**

```c
CONTAINER_NAME     = "sysadmin-game"
CONTAINER_IMAGE    = "sysadmin-game:latest"
CONTAINER_NETWORK  = "sysadmin-net"
CONTAINER_SSH_PORT = 2222
SSH_USER           = "player"
SSH_PASSWORD       = "datacenter2031"
CMD_BUF            = 512   // Podman command buffer size
SSH_WAIT_S         = 15    // SSH wait seconds
```

**Podman Capabilities:** each container is launched with `--cap-add NET_RAW --cap-add NET_ADMIN --cap-add SYS_PTRACE`, enabling `ping`, `tcpdump`, `nmap`, `strace`, etc.

---

### `infra.c` / `infra.h` — Simulated Physical Infrastructure

Simulates a physical datacenter: racks, servers, network switches, cables. State persists to `~/.sysadmin-game.infra`.

**Types:**

| Type | Key Fields |
|---|---|
| `Rack` | `name`, `units` (height in U) |
| `PhysServer` | `name`, `rack`, `slot`, `size_u`, `cpu`, `ram_mb`, `disk_gb`, `port` (SSH), `powered` |
| `PhysSwitch` | `name`, `rack`, `slot`, `size_u`, `ports`, `powered` |
| `Cable` | `server`, `nic`, `sw_name`, `sw_port` |

**Return codes for `infra_*_add()` functions:**

| Code | Meaning |
|---|---|
| `0` | Success |
| `-1` | Limit reached |
| `-2` | Unknown rack |
| `-3` | Name already in use |
| `-4` | Invalid slot (< 1) |
| `-5` | Slot occupied (overlap) |

**Deletion Constraints:**
- `/rack delete`: fails if rack still contains servers or switches.
- `/server delete`: fails if server is powered on. Also removes all its cables.
- `/switch delete`: fails if switch is powered on. Removes associated cables and deletes backing Podman network.

**`infra_server_nets(inf, name, nets, max)`** — Returns names of switches the server is cabled to (used to connect container to correct Podman networks at startup).

---

### `vterm.c` / `vterm.h` — Custom VT100 Emulator

Terminal emulator written from-scratch to parse SSH shell ANSI/VT100 output and render it in an ncurses window.

**Structures:**

- `VCell` — a cell: character, ncurses color pair, attributes (`A_BOLD`, `A_UNDERLINE`, `A_REVERSE`).
- `VTerm` — complete state: main buffer (`screen`), alternate buffer (`altscreen`), cursor position, scroll region, current attributes, parser, scrollback ring-buffer.

**State Machine (`vterm_process`):**

```
ST_NORMAL → ESC → ST_ESC → '[' → ST_CSI → command → ST_NORMAL
                          → ']' → ST_OSC → BEL/ST → ST_NORMAL
                          → '(' → ST_ESC_IGNORE → ST_NORMAL
```

**Supported ANSI Sequences:**

| Category | Codes |
|---|---|
| Cursor movement | CUU/CUD/CUF/CUB (A-D), CUP/HVP (H/f), CHA (G), VPA (d) |
| Erase | ED (J: 0/1/2), EL (K: 0/1/2) |
| Scroll | SU/SD (S/T), DECSTBM region (r) |
| Insert/delete | IL/DL (L/M), ICH/DCH (@/P) |
| SGR attributes | `m`: bold, underline, inverse, colors 30-37/40-47, 256-color (38;5;n / 48;5;n) |
| Alternate screen | `?1049h`/`?1049l`, `?47h`/`?47l` (for vim, less, htop) |
| Cursor save | ESC 7 / ESC 8 (DECSC/DECRC) |

**Color Management:** cache `pair_cache[fg][bg]` avoids re-allocating identical ncurses color pairs. Pairs 1-7 reserved for UI; VTerm starts at `first_pair = 8`.

**Scrollback:** circular ring-buffer of `SCROLLBACK_LINES = 500` lines. Fed only when top line leaves screen via natural scroll (not altscreen, to avoid polluting with vim/htop). `vterm_scroll(vt, delta)` adjusts `sb_offset`; `vterm_render()` uses buffer if `sb_offset > 0`.

**`vterm_render(vt, win)`** — Redraws entire window cell-by-cell with `waddch()`, then repositions ncurses cursor.

---

### `history.c` / `history.h` — Command History

Circular ring-buffer of `HISTORY_MAX` (64) entries.

- `history_push()` — ignores consecutive duplicates, resets cursor to end.
- `history_prev()` — saves current input before first scroll up (`saved`), decrements cursor.
- `history_next()` — increments cursor, restores `saved` if returning to current position.

---

### `Containerfile` — Podman Image

Based on `debian:bookworm-slim`. Installs openssh-server, common Unix tools, and a wide array of sysadmin tools:

| Category | Packages |
|---|---|
| Network | `iproute2`, `iputils-ping`, `traceroute`, `tcpdump`, `nmap`, `netcat-openbsd`, `dnsutils`, `mtr-tiny`, `iperf3`, `iptables`, `isc-dhcp-client` |
| Monitoring | `htop`, `procps`, `lsof` |
| Debugging | `strace`, `gcc`, `make` |
| Files | `vim-tiny`, `nano`, `less`, `curl`, `rsync`, `tree`, `jq` |

Creates `player` user (password `datacenter2031`) with passwordless sudo. Enables SSH password auth, disables root login.

---

## Compilation

```bash
make        # compile → ./game
make re     # full recompilation
make clean  # removes build/ and ./game
make test   # runs 3 unit test suites
```

**Dependencies:** `gcc`, `libncurses-dev`, `libssh2-dev`, `podman`.

---

## Unit Tests

Three suites in `tests/`:

| File | Tests | Covers |
|---|---|---|
| `test_infra.c` | 31 | Racks, servers, switches, cables, persistence |
| `test_history.c` | 10 | Push, dedup, navigation prev/next, restore |
| `test_vterm.c` | 21 | Lifecycle, display, CSI, SGR, scrollback, altscreen |

Tests `test_history` and `test_vterm` use a stub `tests/ncurses.h` (empty inline types and functions) to compile without ncurses or libssh2.

```bash
make test
# ✓  All suites passed  (62 tests total)
```

---

## Running

```bash
./game
```

On first launch, the Podman image is built automatically (~30 s). Deployed containers remain persistent between sessions.

---

## Game Commands

Game commands are prefixed with `/`. Everything else is sent directly to the active container shell.

### Containers

```
/deploy <name> [<image>] [--networks net1,net2]   deploy a container
/stop <name>                                       stop a container
```

### Virtual Networks

```
/network create <name>    create an isolated Podman network
/network delete <name>    delete a Podman network
```

### Physical Infrastructure

```
/rack create <name> [<units>]                       create a rack (default: 42U)
/rack list                                         list all racks
/rack show <name>                                   display rack contents
/rack delete <name>                                 delete empty rack

/server add <name> <rack> <slot> [<U> <cpu> <ram> <disk>]   install server
/server poweron <name>                              start (deploy container)
/server poweroff <name>                             stop (stop container)
/server list [<rack>]                              list servers
/server delete <name>                               remove stopped server

/switch add <name> <rack> <slot> [<U> [<ports>]]   install switch
/switch poweron <name>                              power on switch
/switch poweroff <name>                             power off switch
/switch list [<rack>]                              list switches
/switch delete <name>                               remove stopped switch

/cable connect <server>:<nic> <switch>:<port>    cable NIC to switch port
/cable disconnect <server>:<nic>                  disconnect cable
/cable list                                        list all cables
```

> Each switch creates a backing Podman network. Servers cabled to the switch are connected to that network at `poweron`, allowing them to communicate.

### Utilities

```
/exit   /quit   quit the game
```

---

## Navigation

```
F1–F8                 switch between containers/shells
↑ / ↓                 command history
PageUp / PageDown      scroll terminal scrollback (500 lines)
Shift+PageUp/Down      scroll narrator panel
Ctrl+C / Ctrl+D …      passed directly to container process
```

---

## Router Scenario Example

```
/rack create room-a 42
/switch add core-sw room-a 1
/network create lan-a
/network create lan-b
/switch poweron core-sw
/deploy router01 --networks lan-a,lan-b
# → F2 :
echo 1 > /proc/sys/net/ipv4/ip_forward
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
```

---

## Persistence

| File | Content |
|---|---|
| `~/.sysadmin-game.state` | Deployed containers with their additional networks |
| `~/.sysadmin-game.infra` | Racks, servers, switches, cables, powered state |

Both files are reread at startup. The main container (`sysadmin-game`) is always recreated, never persisted in `.state`.
