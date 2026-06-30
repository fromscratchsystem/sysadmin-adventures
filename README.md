# Sysadmin Adventures

Jeu de terminal en C dans lequel le joueur gère un datacenter fictif situé en 2031. L'interface est entièrement en ncurses ; chaque « machine » est un conteneur Podman accessible via SSH.

---

## Architecture globale

```
┌──────────────────────────────────────────────────────────────┐
│  main.c  — boucle principale + 7 handlers de commandes       │
├───────────┬─────────────┬────────────┬──────────┬────────────┤
│  ui.c     │  shell.c    │container.c │ infra.c  │ history.c  │
│ ncurses   │  SSH/PTY    │  Podman    │ datacenter│ commandes  │
│ layout    │  libssh2    │  CLI       │ physique │ historique │
├───────────┴─────────────┴────────────┴──────────┴────────────┤
│  vterm.c — émulateur VT100 maison (parseur ANSI + scrollback)│
└──────────────────────────────────────────────────────────────┘
```

### Flux de données

```
Clavier → main.c → shell_send_line() → libssh2 → PTY conteneur
                                                       │
Écran ← vterm_render() ← vterm_process() ← libssh2 ←──┘
```

---

## Modules

### `defs.h` — Types et constantes partagés

Inclus par tous les autres fichiers. Définit :

| Symbole | Valeur | Rôle |
|---|---|---|
| `STATUS_HEIGHT` | 1 | Hauteur de la barre de statut |
| `NARRATOR_RATIO` | 0.35 | Part de l'écran réservée au narrateur |
| `HISTORY_MAX` | 64 | Taille de la ring-buffer d'historique |
| `CMD_MAX` | 256 | Longueur max d'une commande |
| `MAX_SHELLS` | 8 | Nombre max de conteneurs simultanés |
| `COL_*` | 1-7 | Paires de couleurs ncurses réservées à l'UI |

**Structures principales :**

- `Panel` — fenêtre ncurses avec bordure + zone de contenu (`border` / `inner`).
- `Layout` — ensemble complet des fenêtres : `narrator`, `tab_bar`, `shell`, `status`, `input_win`.
- `Shell` — état d'une connexion SSH : socket TCP, session/canal libssh2, émulateur `VTerm`, indicateur d'activité, réseaux additionnels.
- `History` — ring-buffer circulaire de `HISTORY_MAX` entrées avec curseur de navigation.

---

### `main.c` — Point d'entrée et boucle principale

**Initialisation (ordre important)**

1. `container_init_network()` — crée le réseau Podman avant ncurses (appels `system()` qui écrivent sur stderr).
2. `container_ensure_running()` — build/démarre le conteneur principal.
3. `infra_load()` + recréation des réseaux Podman des switches (idempotent).
4. `sigaction(SIGWINCH, …)` — enregistrement du handler de redimensionnement.
5. `init_ncurses()` + `create_layout()` — création de l'UI.
6. `attach_shell()` — ouverture de la première connexion SSH.
7. `state_load()` — reconnexion aux conteneurs déployés lors d'une session précédente.

**Boucle principale (`select`-based)**

À chaque itération :
1. Traitement du signal SIGWINCH en attente (rebuild du layout, redimensionnement des PTY).
2. `select()` sur stdin + tous les sockets SSH actifs (timeout 100 ms).
3. Lecture de la sortie SSH pour chaque shell actif → `shell_read_output()`.
4. Traitement des frappes clavier.

**Structure des handlers de commandes**

Les commandes jeu (`/…`) sont délégués à sept fonctions statiques. `main()` se contente d'un dispatch linéaire :

```c
cmd_network()   // /network create|delete
cmd_deploy()    // /deploy
cmd_stop()      // /stop
cmd_rack()      // /rack create|list|show|delete
cmd_server()    // /server add|poweron|poweroff|list|delete
cmd_switch()    // /switch add|poweron|poweroff|list|delete
cmd_cable()     // /cable connect|disconnect|list
```

Chaque handler reçoit un `ShCtx *` (pointeurs vers `shells`, `nshells`, `active`, `running`, `layout`) pour les commandes qui manipulent des shells.

**Persistance**

- `~/.sysadmin-game.state` — liste des conteneurs déployés (restaurés au lancement).
- `~/.sysadmin-game.infra` — état de l'infrastructure physique (racks, serveurs, switches, câbles).

---

### `ui.c` / `ui.h` — Interface ncurses

```
┌─ NARRATEUR ──────────────────────────────────────┐  ← Panel (35 % écran)
│ > Bien. Tu existes. Felicitations.               │
└──────────────────────────────────────────────────┘
[F1 player@datacenter]  [F2 web01!]                   ← tab_bar (1 ligne)
┌─ TERMINAL ───────────────────────────────────────┐  ← Panel (reste)
│                                                  │
└──────────────────────────────────────────────────┘
$ █                                                   ← input_win
 2031-03-14 03:47        SLA: 99.2%      Tickets: 3   ← status
```

**Fonctions principales :**

- `init_ncurses()` — locale, couleurs, 7 paires de couleurs (vert narrateur, cyan terminal, etc.).
- `create_layout()` / `destroy_layout()` / `handle_resize()` — construction et reconstruction du layout. `handle_resize()` détruit l'ancien layout, recrée tout et redimensionne tous les PTY actifs.
- `draw_tabs()` — affiche `[Fn nom]` pour chaque shell ; met en gras + `!` si activité hors focus.
- `draw_status()` — date, SLA, tickets en barre verte inversée.
- `narrator_say(p, msg)` — pousse une ligne dans le ring-buffer narrateur et rafraîchit si en vue live.
- `narrator_printf(p, fmt, …)` — raccourci `vsnprintf` + `narrator_say` (évite les `char msg[N]; snprintf` en dehors de ui.c).
- `narrator_scroll(p, delta)` — déplace la vue du narrateur ; `delta > 0` = remonter.
- `redraw_input()` — réaffiche le prompt `$ ` puis le buffer de saisie.

**Historique narrateur**

Le contenu du panel narrateur est stocké dans un ring-buffer statique de 512 lignes × 256 caractères dans `ui.c`. Ce buffer survit aux redimensionnements (contrairement aux fenêtres ncurses qui sont détruites et recréées).

---

### `shell.c` / `shell.h` — Connexion SSH via libssh2

**`shell_spawn(rows, cols, name, port)`**

1. `socket()` + `connect()` vers `127.0.0.1:port`.
2. `libssh2_session_init()` + `libssh2_session_handshake()`.
3. `libssh2_userauth_password()` (user `player`, mot de passe `datacenter2031`).
4. Ouverture d'un canal SSH + requête PTY `xterm-256color`.
5. Passage en mode **non-bloquant** pour intégration `select()`.
6. Allocation du `VTerm` avec `first_pair = 8` (paires 1-7 réservées à l'UI).

**`shell_send_line(sh, cmd)`** — Écrit `cmd + "\n"` sur le canal (gère `LIBSSH2_ERROR_EAGAIN`).

**`shell_read_output(sh, win)`** — Lit jusqu'à 4096 octets, boucle jusqu'à `EAGAIN` ou EOF. Passe les données à `vterm_process()`. Si `win != NULL`, appelle `vterm_render()`.

**`shell_resize(sh, rows, cols)`** — `libssh2_channel_request_pty_size()` + `vterm_resize()`.

**`shell_close(sh)`** — Ferme canal, session, socket et libère le VTerm. Le conteneur Podman reste actif.

---

### `container.c` / `container.h` — Gestion des conteneurs Podman

Toutes les commandes Podman sont lancées via `system()` redirigé vers `/dev/null` (`run_silent()`).

| Fonction | Description |
|---|---|
| `container_init_network()` | Crée le réseau `sysadmin-net` si absent |
| `container_ensure_running()` | Build l'image si absente (~30 s), crée/démarre le conteneur principal sur le port 2222, attend SSH |
| `container_deploy(name, image, port, nets, nnets)` | Déploie un conteneur, le connecte aux réseaux additionnels, attend SSH |
| `container_network_create(name)` | Crée un réseau Podman (idempotent) |
| `container_network_delete(name)` | Supprime un réseau Podman |
| `container_stop(name)` | `podman stop` sans suppression |
| `container_is_running(name)` | Teste si le conteneur est en cours d'exécution |

**`wait_for_ssh(port, SSH_WAIT_S)`** — Sonde le port TCP toutes les secondes, jusqu'à `SSH_WAIT_S = 15` tentatives.

La commande `podman run` (avec capabilities) est centralisée dans le helper privé `container_start_new()` pour éviter la duplication entre `container_ensure_running()` et `container_deploy()`.

**Constantes importantes :**

```c
CONTAINER_NAME     = "sysadmin-game"
CONTAINER_IMAGE    = "sysadmin-game:latest"
CONTAINER_NETWORK  = "sysadmin-net"
CONTAINER_SSH_PORT = 2222
SSH_USER           = "player"
SSH_PASSWORD       = "datacenter2031"
CMD_BUF            = 512   // taille des buffers de commandes Podman
SSH_WAIT_S         = 15    // secondes d'attente SSH
```

**Capabilities Podman :** chaque conteneur est lancé avec `--cap-add NET_RAW --cap-add NET_ADMIN --cap-add SYS_PTRACE`, ce qui autorise `ping`, `tcpdump`, `nmap`, `strace`, etc.

---

### `infra.c` / `infra.h` — Infrastructure physique simulée

Simule un datacenter physique : baies (racks), serveurs, switches réseau, câbles. L'état est persisté dans `~/.sysadmin-game.infra`.

**Types :**

| Type | Champs clés |
|---|---|
| `Rack` | `name`, `units` (hauteur en U) |
| `PhysServer` | `name`, `rack`, `slot`, `size_u`, `cpu`, `ram_mb`, `disk_gb`, `port` (SSH), `powered` |
| `PhysSwitch` | `name`, `rack`, `slot`, `size_u`, `ports`, `powered` |
| `Cable` | `server`, `nic`, `sw_name`, `sw_port` |

**Codes de retour des fonctions `infra_*_add()` :**

| Code | Signification |
|---|---|
| `0` | Succès |
| `-1` | Limite atteinte |
| `-2` | Baie inconnue |
| `-3` | Nom déjà utilisé |
| `-4` | Slot invalide (< 1) |
| `-5` | Slot occupé (chevauchement) |

**Contraintes de suppression :**
- `/rack delete` : échoue si la baie contient encore des serveurs ou switches.
- `/server delete` : échoue si le serveur est allumé. Retire aussi tous ses câbles.
- `/switch delete` : échoue si le switch est allumé. Retire les câbles associés et supprime le réseau Podman backing.

**`infra_server_nets(inf, name, nets, max)`** — Retourne les noms des switches auxquels le serveur est câblé (utilisé pour connecter le conteneur aux bons réseaux Podman au démarrage).

---

### `vterm.c` / `vterm.h` — Émulateur VT100 maison

Émulateur de terminal écrit from-scratch pour interpréter la sortie ANSI/VT100 du shell SSH et la rendre dans une fenêtre ncurses.

**Structures :**

- `VCell` — une cellule : caractère, paire de couleurs ncurses, attributs (`A_BOLD`, `A_UNDERLINE`, `A_REVERSE`).
- `VTerm` — état complet : buffer principal (`screen`), buffer alternatif (`altscreen`), position curseur, région de scroll, attributs courants, parseur, ring-buffer de scrollback.

**Machine à états (`vterm_process`)** :

```
ST_NORMAL → ESC → ST_ESC → '[' → ST_CSI → commande → ST_NORMAL
                          → ']' → ST_OSC → BEL/ST → ST_NORMAL
                          → '(' → ST_ESC_IGNORE → ST_NORMAL
```

**Séquences ANSI supportées :**

| Catégorie | Codes |
|---|---|
| Déplacements curseur | CUU/CUD/CUF/CUB (A-D), CUP/HVP (H/f), CHA (G), VPA (d) |
| Effacement | ED (J : 0/1/2), EL (K : 0/1/2) |
| Scroll | SU/SD (S/T), région DECSTBM (r) |
| Insertion/suppression | IL/DL (L/M), ICH/DCH (@/P) |
| Attributs SGR | `m` : gras, souligné, inversé, couleurs 30-37/40-47, 256-color (38;5;n / 48;5;n) |
| Écran alternatif | `?1049h`/`?1049l`, `?47h`/`?47l` (pour vim, less, htop) |
| Sauvegarde curseur | ESC 7 / ESC 8 (DECSC/DECRC) |

**Gestion des couleurs :** cache `pair_cache[fg][bg]` évite de ré-allouer des paires ncurses identiques. Les paires 1-7 sont réservées à l'UI ; le VTerm part de `first_pair = 8`.

**Scrollback :** ring-buffer circulaire de `SCROLLBACK_LINES = 500` lignes. Alimenté uniquement quand la ligne du haut quitte l'écran en scroll naturel (pas en altscreen, pour ne pas polluer avec vim/htop). `vterm_scroll(vt, delta)` ajuste `sb_offset` ; `vterm_render()` utilise ce buffer si `sb_offset > 0`.

**`vterm_render(vt, win)`** — Redessine toute la fenêtre cellule par cellule avec `waddch()`, puis repositionne le curseur ncurses.

---

### `history.c` / `history.h` — Historique des commandes

Ring-buffer circulaire de `HISTORY_MAX` (64) entrées.

- `history_push()` — ignore les doublons consécutifs, remet le curseur en fin d'historique.
- `history_prev()` — sauvegarde la saisie en cours avant la première remontée (`saved`), décrémente le curseur.
- `history_next()` — incrémente le curseur, restaure `saved` si on revient à la position courante.

---

### `Containerfile` — Image Podman

Basée sur `debian:bookworm-slim`. Installe openssh-server, outils Unix courants, et une large palette d'outils sysadmin :

| Catégorie | Paquets |
|---|---|
| Réseau | `iproute2`, `iputils-ping`, `traceroute`, `tcpdump`, `nmap`, `netcat-openbsd`, `dnsutils`, `mtr-tiny`, `iperf3`, `iptables`, `isc-dhcp-client` |
| Monitoring | `htop`, `procps`, `lsof` |
| Débogage | `strace`, `gcc`, `make` |
| Fichiers | `vim-tiny`, `nano`, `less`, `curl`, `rsync`, `tree`, `jq` |

Crée l'utilisateur `player` (mot de passe `datacenter2031`) avec sudo sans mot de passe. Active l'authentification par mot de passe SSH, désactive le login root.

---

## Compilation

```bash
make        # compile → ./game
make re     # recompilation complète
make clean  # supprime build/ et ./game
make test   # lance les 3 suites de tests unitaires
```

**Dépendances :** `gcc`, `libncurses-dev`, `libssh2-dev`, `podman`.

---

## Tests unitaires

Trois suites dans `tests/` :

| Fichier | Tests | Couvre |
|---|---|---|
| `test_infra.c` | 31 | Racks, serveurs, switches, câbles, persistance |
| `test_history.c` | 10 | Push, dédup, navigation prev/next, restauration |
| `test_vterm.c` | 21 | Lifecycle, affichage, CSI, SGR, scrollback, altscreen |

Les tests `test_history` et `test_vterm` utilisent un stub `tests/ncurses.h` (types et fonctions inline vides) afin de compiler sans ncurses ni libssh2.

```bash
make test
# ✓  Toutes les suites passées  (62 tests au total)
```

---

## Lancement

```bash
./game
```

Au premier lancement, l'image Podman est construite automatiquement (~30 s). Les conteneurs déployés restent persistants entre les sessions.

---

## Commandes du jeu

Les commandes jeu sont préfixées par `/`. Tout le reste est envoyé directement au shell du conteneur actif.

### Conteneurs

```
/deploy <nom> [<image>] [--networks net1,net2]   déploie un conteneur
/stop <nom>                                       arrête un conteneur
```

### Réseaux virtuels

```
/network create <nom>    crée un réseau Podman isolé
/network delete <nom>    supprime un réseau Podman
```

### Infrastructure physique

```
/rack create <nom> [<units>]                       crée une baie (défaut : 42U)
/rack list                                         liste toutes les baies
/rack show <nom>                                   affiche le contenu d'une baie
/rack delete <nom>                                 supprime une baie vide

/server add <nom> <rack> <slot> [<U> <cpu> <ram> <disk>]   installe un serveur
/server poweron <nom>                              démarre (déploie le conteneur)
/server poweroff <nom>                             éteint (arrête le conteneur)
/server list [<rack>]                              liste les serveurs
/server delete <nom>                               retire un serveur éteint

/switch add <nom> <rack> <slot> [<U> [<ports>]]   installe un switch
/switch poweron <nom>                              allume le switch
/switch poweroff <nom>                             éteint le switch
/switch list [<rack>]                              liste les switches
/switch delete <nom>                               retire un switch éteint

/cable connect <serveur>:<nic> <switch>:<port>    câble une NIC à un port switch
/cable disconnect <serveur>:<nic>                  déconnecte un câble
/cable list                                        liste tous les câbles
```

> Chaque switch crée un réseau Podman backing. Les serveurs câblés à ce switch sont connectés à ce réseau au `poweron`, ce qui leur permet de communiquer entre eux.

### Utilitaires

```
/exit   /quit   quitte le jeu
```

---

## Navigation

```
F1–F8                 basculer entre conteneurs/shells
↑ / ↓                 historique des commandes
PageUp / PageDown      scroll du scrollback terminal (500 lignes)
Shift+PageUp/Down      scroll du panel narrateur
Ctrl+C / Ctrl+D …      transmis directement au processus dans le conteneur
```

---

## Scénario routeur

```
/rack create salle-a 42
/switch add core-sw salle-a 1
/network create lan-a
/network create lan-b
/switch poweron core-sw
/deploy router01 --networks lan-a,lan-b
# → F2 :
echo 1 > /proc/sys/net/ipv4/ip_forward
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
```

---

## Persistance

| Fichier | Contenu |
|---|---|
| `~/.sysadmin-game.state` | Conteneurs déployés avec leurs réseaux additionnels |
| `~/.sysadmin-game.infra` | Racks, serveurs, switches, câbles, état allumé/éteint |

Les deux fichiers sont relus au démarrage. Le conteneur principal (`sysadmin-game`) est toujours recréé, jamais persisté dans `.state`.
