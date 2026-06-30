# Contributing

## Prerequisites

- GCC (C99)
- libncurses-dev
- libssh2-1-dev
- Podman

```sh
make        # compile game → ./game
make test   # run test suites
make re     # full recompilation
```

## Architecture

| File | Role |
|---|---|
| `src/infra.c/h` | Physical model: racks, servers, switches, cables |
| `src/hardware.c/h` | Component catalog (CPU/RAM/Disk) and chassis models |
| `src/container.c/h` | Podman container management (simulated servers) |
| `src/vterm.c/h` | Terminal emulator (VT100/ANSI) |
| `src/ui.c/h` | ncurses rendering: panels, tabs, narrator |
| `src/main.c` | Game loop, `/` command dispatch |
| `data/` | Editable catalogs (no recompilation needed) |
| `tests/` | Unit test suites |

**Isolation Rule:** `infra.c` must not include `hardware.h`. Test binaries (`test_infra`) link only their target module — avoid cross-dependencies that break this isolation.

## Data

Hardware catalogs are in `data/` in pipe-delimited format:

```
# Comment
type | id | label | cores | ghz10 | mem_gen | size_mb | size_gb | disk_type | iops
```

To add a component or server model: edit the text file, restart the game. No recompilation.

## Contribution Rules

### 1. Tests First

Any bug fix or new validation must be covered by a test **before** commit.

```sh
# Add test to tests/test_infra.c (or test_vterm.c, test_history.c)
make test   # must pass
git add ... && git commit
```

Test must cover: nominal case, lower bound, upper bound, invalid values when applicable.

### 2. Unit Tests

Tests use `tests/framework.h` (no external dependencies):

```c
TEST(my_test) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    ASSERT_EQ(infra_rack_create(&inf, "rack-A", 10), -3); /* duplicate */
}
```

Add call to test's `main()`. `make test` should display `✓  All suites passed`.

### 3. Return Codes

`infra_*` and `hw_*` functions return `0` for success and negative integers for errors. Each code must be documented in the corresponding `.h` file. Add a new code if the error situation is distinct — don't reuse an existing code for a different case.

### 4. C Style

- Standard: C99, `-Wall -Wextra -pedantic`, zero warnings.
- Naming: `snake_case` for everything (functions, variables, types via `typedef`).
- No comments except when the **why** is non-obvious (hidden constraint, workaround, surprise). Don't comment the **what**.
- No `malloc` / `free`: all structures use fixed-size static arrays defined by constants in `.h` files (`MAX_RACKS`, `HW_RAM_SLOTS`, etc.).
- UI strings are in English.

### 5. Commit Messages

Format: `type: short description` (Conventional Commits, in English).

Types: `feat` / `fix` / `test` / `refactor` / `docs` / `chore`.

```
feat: add power management for switches
fix: reject negative slot numbers in server_add
test: cover cable port boundary conditions
```

One commit = one intention. Don't mix refactoring and new features in same commit.

### 6. Persistence

Infrastructure objects are saved to `~/.sysadmin-game.infra` via `infra_save/load`. If a field is added to a persisted structure:

1. Extend format in `infra_save()`.
2. Keep backward compatibility in `infra_load()` by checking fields read by `sscanf`.
3. Add round-trip test to `test_infra.c`.
