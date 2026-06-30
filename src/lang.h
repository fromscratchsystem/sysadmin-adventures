#ifndef LANG_H
#define LANG_H

/*
 * Système de traduction minimaliste.
 * Le texte source (français) sert de clé : T("Baie inconnue.") retourne
 * la traduction si elle existe dans le fichier chargé, sinon la clé elle-même.
 * Sans fichier chargé, T() est transparent — le jeu reste en français.
 */

/* Charge un fichier de traduction (format KEY=valeur, # = commentaire).
 * Retourne 0 si OK, -1 si le fichier ne peut pas être ouvert. */
int lang_load(const char *path);

/* Retourne le code langue à 2 lettres déduit de la variable d'env LANG
 * (ex : "fr_FR.UTF-8" → "fr", "en_US.UTF-8" → "en").
 * Défaut : "fr". */
const char *lang_detect(void);

/* Cherche key dans le langpack chargé ; retourne la traduction ou key. */
const char *lang_get(const char *key);

#define T(key) lang_get(key)

#endif /* LANG_H */
