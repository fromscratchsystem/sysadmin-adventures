#ifndef HISTORY_H
#define HISTORY_H

#include "defs.h"

/*
 * history_push — ajoute une commande à l'historique.
 * Les doublons consécutifs sont ignorés (comportement bash par défaut).
 * Le curseur est remis en fin d'historique après chaque ajout.
 */
void history_push(History *h, const char *cmd);

/*
 * history_prev — recule dans l'historique (touche ↑).
 * Copie la commande dans buf (taille maxlen).
 * Retourne 1 si une entrée a été chargée, 0 si déjà au plus ancien.
 *
 * saved : buffer où sauvegarder la saisie courante avant la première
 *         remontée (restauré par history_next quand on revient à la fin).
 */
int history_prev(History *h, char *buf, int maxlen, char *saved);

/*
 * history_next — avance dans l'historique (touche ↓).
 * Retourne 1 si une entrée a été chargée, 0 si déjà en fin.
 */
int history_next(History *h, char *buf, int maxlen, const char *saved);

#endif /* HISTORY_H */
