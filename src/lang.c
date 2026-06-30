#include "lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LANG_ENTRIES 256

typedef struct {
    char key[256];
    char val[256];
} LangEntry;

static LangEntry g_lang[MAX_LANG_ENTRIES];
static int       g_nlang = 0;

int lang_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    g_nlang = 0;
    char line[600];

    while (fgets(line, sizeof(line), f) && g_nlang < MAX_LANG_ENTRIES) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        strncpy(g_lang[g_nlang].key, line,   255);
        strncpy(g_lang[g_nlang].val, eq + 1, 255);
        g_lang[g_nlang].key[255] = '\0';
        g_lang[g_nlang].val[255] = '\0';
        g_nlang++;
    }

    fclose(f);
    return 0;
}

const char *lang_detect(void) {
    static char code[8];
    const char *env = getenv("LANG");
    if (!env || env[0] == '\0') { strcpy(code, "fr"); return code; }

    int i = 0;
    while (i < 7 && env[i] && env[i] != '_' && env[i] != '.') {
        code[i] = env[i];
        i++;
    }
    code[i] = '\0';
    return code[0] ? code : "fr";
}

const char *lang_get(const char *key) {
    for (int i = 0; i < g_nlang; i++)
        if (strcmp(g_lang[i].key, key) == 0)
            return g_lang[i].val;
    return key;
}
