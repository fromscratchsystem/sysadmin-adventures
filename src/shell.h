#ifndef SHELL_H
#define SHELL_H

#include "defs.h"
#include "ui.h"

/*
 * shell_spawn — ouvre une session SSH vers un conteneur Podman.
 *
 * name      : label de l'onglet (ex: "player@datacenter", "web01")
 * port      : port hôte sur lequel le conteneur expose SSH
 */
Shell shell_spawn(int shell_rows, int shell_cols,
                  const char *name, int port);

/*
 * shell_send_raw — envoie len octets bruts sans ajout de '\n'.
 * Utilisé pour transmettre les caractères de contrôle (Ctrl+C, Ctrl+D…).
 */
void shell_send_raw(Shell *sh, const char *buf, int len);

/*
 * shell_send_line — envoie une commande suivie de '\n' via le canal SSH.
 * Sans effet si sh->alive == 0.
 */
void shell_send_line(Shell *sh, const char *cmd);

/*
 * shell_read_output — lit tout ce qui est disponible sur le canal SSH
 * et alimente le VTerm. Si win != NULL, rend l'écran dans la fenêtre.
 * Passer NULL pour les shells hors focus (VTerm mis à jour en arrière-plan).
 */
void shell_read_output(Shell *sh, WINDOW *win);

/*
 * shell_resize — notifie le pseudo-terminal distant de la nouvelle taille.
 */
void shell_resize(Shell *sh, int rows, int cols);

/*
 * shell_close — ferme proprement le canal et la session SSH.
 * Le conteneur reste en cours d'exécution (persistant).
 */
void shell_close(Shell *sh);

#endif /* SHELL_H */
