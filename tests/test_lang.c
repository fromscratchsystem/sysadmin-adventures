#include "framework.h"
#include "lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Helpers ─────────────────────────────────────────────────── */

static const char *tmp_path(void) {
    return "/tmp/test_lang_sysadmin.txt";
}

static void write_tmp(const char *content) {
    FILE *f = fopen(tmp_path(), "w");
    if (!f) { perror("fopen"); exit(1); }
    fputs(content, f);
    fclose(f);
}

/* ─── Tests ───────────────────────────────────────────────────── */

TEST(get_before_load_returns_key) {
    /* Doit être le premier test — aucun lang_load() appelé avant. */
    ASSERT_STR(lang_get("Clef absente."), "Clef absente.");
}

TEST(load_valid_file_returns_0) {
    write_tmp("Baie inconnue.=Unknown rack.\n");
    ASSERT_EQ(lang_load(tmp_path()), 0);
}

TEST(get_known_key_returns_translation) {
    write_tmp("Baie inconnue.=Unknown rack.\nServeur inconnu.=Unknown server.\n");
    lang_load(tmp_path());
    ASSERT_STR(lang_get("Baie inconnue."), "Unknown rack.");
    ASSERT_STR(lang_get("Serveur inconnu."), "Unknown server.");
}

TEST(get_unknown_key_returns_key_itself) {
    write_tmp("Baie inconnue.=Unknown rack.\n");
    lang_load(tmp_path());
    ASSERT_STR(lang_get("Clef absente."), "Clef absente.");
}

TEST(load_ignores_comment_lines) {
    write_tmp("# un commentaire\nBaie inconnue.=Unknown rack.\n# fin\n");
    lang_load(tmp_path());
    ASSERT_STR(lang_get("Baie inconnue."), "Unknown rack.");
}

TEST(load_ignores_empty_lines) {
    write_tmp("\nBaie inconnue.=Unknown rack.\n\n");
    lang_load(tmp_path());
    ASSERT_STR(lang_get("Baie inconnue."), "Unknown rack.");
}

TEST(load_missing_file_returns_minus1) {
    ASSERT_EQ(lang_load("/tmp/fichier_inexistant_sysadmin_99.txt"), -1);
}

TEST(format_string_preserved) {
    write_tmp("Baie '%s' (%dU) creee.=Rack '%s' (%dU) created.\n");
    lang_load(tmp_path());
    ASSERT_STR(lang_get("Baie '%s' (%dU) creee."), "Rack '%s' (%dU) created.");
}

TEST(reload_replaces_entries) {
    write_tmp("Baie inconnue.=Unknown rack.\n");
    lang_load(tmp_path());
    ASSERT_STR(lang_get("Baie inconnue."), "Unknown rack.");

    write_tmp("Baie inconnue.=Rack not found.\n");
    lang_load(tmp_path());
    ASSERT_STR(lang_get("Baie inconnue."), "Rack not found.");
}

/* ─── Suite ───────────────────────────────────────────────────── */

int main(void) {
    printf("Suite : lang\n");

    get_before_load_returns_key();
    load_valid_file_returns_0();
    get_known_key_returns_translation();
    get_unknown_key_returns_key_itself();
    load_ignores_comment_lines();
    load_ignores_empty_lines();
    load_missing_file_returns_minus1();
    format_string_preserved();
    reload_replaces_entries();

    RESULTS();
}
