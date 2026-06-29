#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

static int _t_run  = 0;
static int _t_pass = 0;
static int _t_ok   = 1;

/*
 * TEST(name) { ... }
 * Définit une fonction de test dont le résultat (PASS/FAIL) est affiché
 * automatiquement à l'appel. Chaque ASSERT raté marque le test FAIL.
 */
#define TEST(name) \
    static void _body_##name(void); \
    static void name(void) { \
        _t_ok = 1; \
        _body_##name(); \
        _t_run++; \
        if (_t_ok) { _t_pass++; printf("  PASS  " #name "\n"); } \
        else            printf("  FAIL  " #name "\n"); \
    } \
    static void _body_##name(void)

/* Assertions ─────────────────────────────────────────────────── */
#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("         └─ echec ligne %d : %s\n", __LINE__, #expr); \
        _t_ok = 0; \
    } \
} while (0)

#define ASSERT_EQ(a, b)  ASSERT((a) == (b))
#define ASSERT_NE(a, b)  ASSERT((a) != (b))
#define ASSERT_GT(a, b)  ASSERT((a) >  (b))
#define ASSERT_STR(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_NULL(p)   ASSERT((p) == NULL)
#define ASSERT_NN(p)     ASSERT((p) != NULL)

/* À mettre à la fin du main() ────────────────────────────────── */
#define RESULTS() do { \
    printf("\n%d/%d passe(s)", _t_pass, _t_run); \
    if (_t_pass < _t_run) printf(", %d ECHEC(S)", _t_run - _t_pass); \
    puts(""); \
    return (_t_pass < _t_run) ? 1 : 0; \
} while (0)

#endif /* TEST_FRAMEWORK_H */
