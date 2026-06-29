# Sysadmin Adventures

Jeu de terminal en C dans lequel le joueur gère un datacenter fictif situé en 2031. L'interface est entièrement en ncurses ; chaque « machine » est un conteneur Podman accessible via SSH.

---

## Architecture globale

```
┌─────────────────────────────────────────────────────┐
│  main.c  — boucle principale, narrateur, commandes  │
├──────────┬──────────────┬────────────┬──────────────┤
│  ui.c    │  shell.c     │ container.c│  history.c   │
│ ncurses  │  SSH/PTY     │  Podman    │  historique  │
│ layout   │  libssh2     │  CLI       │  commandes   │
├──────────┴──────────────┴────────────┴──────────────┤
│  vterm.c — émulateur VT100 maison (parseur ANSI)    │
└─────────────────────────────────────────────────────┘
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
- `Shell` — état d'une connexion SSH vers un conteneur : socket TCP, session/canal libssh2, émulateur `VTerm`, indicateur d'activité.
- `History` — ring-buffer circulaire de `HISTORY_MAX` entrées avec curseur de navigation.

---

### `main.c` — Point d'entrée et boucle principale

**Initialisation (ordre important)**

1. `container_init_network()` — crée le réseau Podman avant ncurses (appels `system()` qui écrivent sur stderr).
2. `container_ensure_running()` — build/démarre le conteneur principal.
3. `sigaction(SIGWINCH, …)` — enregistrement du handler de redimensionnement.
4. `init_ncurses()` + `create_layout()` — création de l'UI.
5. `attach_shell()` — ouverture de la première connexion SSH.

**Boucle principale (`select`-based)**

À chaque itération :
1. Traitement du signal SIGWINCH en attente (rebuild du layout, redimensionnement des PTY).
2. `select()` sur stdin + tous les sockets SSH actifs (timeout 100 ms).
3. Lecture de la sortie SSH pour chaque shell actif → `shell_read_output()`.
4. Traitement des frappes clavier.

**Commandes interceptées par le jeu**

| Commande | Comportement |
|---|---|
| `exit` / `quit` | Ferme le shell actif et quitte |
| `deploy <nom> [image]` | Déploie un nouveau conteneur et ouvre un onglet |
| `rm … /` ou `rm … /*` | Bloquée par le narrateur |
| Fork bomb `:(){ … }` | Bloquée par le narrateur |
| `ls`, `pwd`, `man`, `sudo`, `help` | Commentaire sarcastique du narrateur, commande exécutée quand même |

**Navigation entre onglets** : touches F1–F8.

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
- `narrator_say()` — `wprintw` avec scroll automatique dans le panel narrateur.
- `redraw_input()` — réaffiche le prompt `$ ` puis le buffer de saisie positionné au curseur.

---

### `shell.c` / `shell.h` — Connexion SSH via libssh2

**`shell_spawn(rows, cols, name, port)`**

1. `socket()` + `connect()` vers `127.0.0.1:port`.
2. `libssh2_session_init()` + `libssh2_session_handshake()`.
3. `libssh2_userauth_password()` (user `player`, mot de passe `datacenter2031`).
4. Ouverture d'un canal SSH + requête PTY `xterm-256color`.
5. Passage en mode **non-bloquant** (`O_NONBLOCK`) pour intégration `select()`.
6. Allocation du `VTerm` avec `first_pair = 8` (paires 1-7 réservées à l'UI).

**`shell_send_line(sh, cmd)`** — Écrit `cmd + "\n"` sur le canal en boucle (gère `LIBSSH2_ERROR_EAGAIN`).

**`shell_read_output(sh, win)`** — Lit jusqu'à 4096 octets par appel, boucle jusqu'à `EAGAIN` ou EOF. Passe les données à `vterm_process()`. Si `win != NULL`, appelle `vterm_render()` pour rafraîchir l'écran.

**`shell_resize(sh, rows, cols)`** — `libssh2_channel_request_pty_size()` + `vterm_resize()`.

**`shell_close(sh)`** — Ferme canal, session, socket et libère le VTerm. Le conteneur Podman reste actif.

---

### `container.c` / `container.h` — Gestion des conteneurs Podman

Toutes les commandes Podman sont lancées via `system()` redirigé vers `/dev/null` (`run_silent()`).

| Fonction | Description |
|---|---|
| `container_init_network()` | Crée le réseau `sysadmin-net` si absent |
| `container_ensure_running()` | Build l'image si absente (~30 s au premier lancement), crée/démarre le conteneur principal sur le port 2222, attend SSH disponible |
| `container_deploy(name, image, port)` | Déploie un conteneur supplémentaire sur le réseau partagé, attend SSH |
| `container_stop(name)` | `podman stop` sans suppression |

**`wait_for_ssh(port, max_s)`** — Sonde le port TCP toutes les secondes jusqu'à `max_s` tentatives.

**Constantes importantes :**

```c
CONTAINER_NAME     = "sysadmin-game"
CONTAINER_IMAGE    = "sysadmin-game:latest"
CONTAINER_NETWORK  = "sysadmin-net"
CONTAINER_SSH_PORT = 2222
SSH_USER           = "player"
SSH_PASSWORD       = "datacenter2031"
```

---

### `vterm.c` / `vterm.h` — Émulateur VT100 maison

Émulateur de terminal écrit from-scratch pour interpréter la sortie ANSI/VT100 du shell SSH et la rendre dans une fenêtre ncurses.

**Structures :**

- `VCell` — une cellule : caractère, paire de couleurs ncurses, attributs (`A_BOLD`, `A_UNDERLINE`, `A_REVERSE`).
- `VTerm` — état complet : buffer principal (`screen`), buffer alternatif (`altscreen`), position curseur, région de scroll, attributs courants, machine à états du parseur.

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

**Gestion des couleurs :** cache `pair_cache[fg][bg]` évite de ré-allouer des paires ncurses identiques. Couleurs indexées 1-8 (ANSI 0-7). Les paires 1-7 sont réservées à l'UI du jeu ; le VTerm part de `first_pair = 8`.

**`vterm_render(vt, win)`** — Redessine toute la fenêtre cellule par cellule avec `waddch()`, puis repositionne le curseur ncurses.

---

### `history.c` / `history.h` — Historique des commandes

Ring-buffer circulaire de `HISTORY_MAX` (64) entrées.

- `history_push()` — ignore les doublons consécutifs, remet le curseur en fin d'historique.
- `history_prev()` — sauvegarde la saisie en cours avant la première remontée (`saved`), décrémente le curseur.
- `history_next()` — incrémente le curseur, restaure `saved` si on revient à la position courante.

---

### `Containerfile` — Image Docker/Podman

Basée sur `debian:bookworm-slim`. Installe : `openssh-server`, outils Unix courants (`bash`, `vim-tiny`, `htop`, `curl`, `iproute2`, etc.).

Crée l'utilisateur `player` (mot de passe `datacenter2031`) avec sudo sans mot de passe. Active l'authentification par mot de passe SSH, désactive le login root.

---

## Compilation

```bash
make        # compile → ./game
make re     # recompilation complète
make clean  # supprime build/ et ./game
```

**Dépendances :** `gcc`, `libncurses-dev`, `libssh2-dev`, `podman`.

---

## Lancement

```bash
./game
```

Au premier lancement, l'image Podman est construite automatiquement (~30 s). Les conteneurs déployés restent persistants entre les sessions.

Les commandes du jeu sont préfixées par `/`. Tout le reste est envoyé directement au shell du conteneur actif.

**Commandes du jeu (`/`) :**

```
/deploy <nom> [<image>] [--networks net1,net2]   # déploie un conteneur
/stop <nom>                                       # arrête un conteneur (persistant)
/network create <nom>                             # crée un réseau Podman
/network delete <nom>                             # supprime un réseau Podman
/exit                                             # quitte le jeu
```

**Navigation :**

```
F1-F8   # basculer entre conteneurs
↑ / ↓   # historique des commandes
Ctrl+C  # interrompt le processus en cours dans le conteneur
```

**Scénario routeur :**

```
/network create lan-a
/network create lan-b
/deploy router01 --networks lan-a,lan-b
# puis dans router01 (F2) :
echo 1 > /proc/sys/net/ipv4/ip_forward
ip route add ...
```

**Persistance :** les conteneurs déployés et leurs réseaux additionnels sont sauvegardés dans `~/.sysadmin-game.state` et restaurés automatiquement au prochain lancement.
