# Contributing

## Prérequis

- GCC (C99)
- libncurses-dev
- libssh2-1-dev
- Podman

```sh
make        # compile le jeu → ./game
make test   # lance les suites de tests
make re     # recompilation complète
```

## Architecture

| Fichier | Rôle |
|---|---|
| `src/infra.c/h` | Modèle physique : racks, serveurs, switches, câbles |
| `src/hardware.c/h` | Catalogue composants (CPU/RAM/Disk) et modèles de châssis |
| `src/container.c/h` | Gestion des conteneurs Podman (serveurs simulés) |
| `src/vterm.c/h` | Émulateur de terminal (VT100/ANSI) |
| `src/ui.c/h` | Rendu ncurses : panneaux, tabs, narrator |
| `src/main.c` | Boucle de jeu, dispatch des commandes `/` |
| `data/` | Catalogues éditables sans recompilation |
| `tests/` | Suites de tests unitaires |

**Règle d'isolation :** `infra.c` ne doit pas inclure `hardware.h`. Les binaires de test (`test_infra`) ne linkent que leur module cible — éviter les dépendances croisées qui brisent cette isolation.

## Données

Les catalogues hardware se trouvent dans `data/` au format pipe-séparé :

```
# Commentaire
type | id | label | cores | ghz10 | mem_gen | size_mb | size_gb | disk_type | iops
```

Pour ajouter un composant ou un modèle de serveur : éditer le fichier texte, relancer le jeu. Pas de recompilation.

## Règles de contribution

### 1. Tests d'abord

Toute correction de bug ou nouvelle validation doit être couverte par un test **avant** le commit.

```sh
# Ajouter le test dans tests/test_infra.c (ou test_vterm.c, test_history.c)
make test   # doit passer
git add ... && git commit
```

Le test doit couvrir le cas nominal, la limite basse, la limite haute et les valeurs invalides quand c'est pertinent.

### 2. Tests unitaires

Les tests utilisent `tests/framework.h` (pas de dépendance externe) :

```c
TEST(mon_test) {
    Infra inf = {0};
    infra_rack_create(&inf, "rack-A", 42);
    ASSERT_EQ(infra_rack_create(&inf, "rack-A", 10), -3); /* doublon */
}
```

Ajouter l'appel dans le `main()` du fichier de test correspondant. `make test` doit afficher `✓  Toutes les suites passées`.

### 3. Codes de retour

Les fonctions `infra_*` et `hw_*` retournent `0` pour le succès et des entiers négatifs pour les erreurs. Chaque code doit être documenté dans le `.h` correspondant. Ajouter un nouveau code si la situation d'erreur est distincte — ne pas réutiliser un code existant pour un cas différent.

### 4. Style C

- Standard : C99, `-Wall -Wextra -pedantic`, zéro warning.
- Nommage : `snake_case` pour tout (fonctions, variables, types via `typedef`).
- Pas de commentaires sauf quand le **pourquoi** est non-évident (contrainte cachée, contournement de bug). Ne pas commenter le *quoi*.
- Pas de `malloc` / `free` : toutes les structures utilisent des tableaux statiques de taille fixe définis par des constantes dans les `.h` (`MAX_RACKS`, `HW_RAM_SLOTS`, etc.).
- Les chaînes de l'interface utilisateur sont en français.

### 5. Messages de commit

Format : `type: description courte` (Conventional Commits, en anglais).

Types : `feat` / `fix` / `test` / `refactor` / `docs` / `chore`.

```
feat: add power management for switches
fix: reject negative slot numbers in server_add
test: cover cable port boundary conditions
```

Un commit = une intention. Ne pas mélanger refactoring et nouvelle fonctionnalité dans le même commit.

### 6. Persistance

Les objets infra sont sauvegardés dans `~/.sysadmin-game.infra` via `infra_save/load`. Si un champ est ajouté à une structure persistée :

1. Étendre le format dans `infra_save()`.
2. Garder la compatibilité ascendante dans `infra_load()` en vérifiant le nombre de champs lus par `sscanf`.
3. Ajouter un test de round-trip dans `test_infra.c`.
