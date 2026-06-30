#ifndef LANG_H
#define LANG_H

/*
 * Minimal translation system.
 * Source text (French) serves as key: T("Unknown rack.") returns
 * the translation if it exists in the loaded file, otherwise the key itself.
 * Without a loaded file, T() is transparent — the game stays in French.
 */

/* Loads a translation file (format KEY=value, # = comment).
 * Returns 0 if OK, -1 if file cannot be opened. */
int lang_load(const char *path);

/* Returns the 2-letter language code derived from the LANG environment variable
 * (e.g.: "fr_FR.UTF-8" → "fr", "en_US.UTF-8" → "en").
 * Default: "fr". */
const char *lang_detect(void);

/* Searches for key in the loaded langpack; returns translation or key. */
const char *lang_get(const char *key);

#define T(key) lang_get(key)

#endif /* LANG_H */
