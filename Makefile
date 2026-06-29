# ─── Configuration ────────────────────────────────────────────
CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c99 -Isrc
LDFLAGS = -lncurses -lssh2

# Répertoires
SRCDIR  = src
OBJDIR  = build

# ─── Sources et objets ────────────────────────────────────────
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

TARGET = game

# ─── Règle principale ─────────────────────────────────────────
.PHONY: all clean re

all: $(OBJDIR) $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "✓  $(TARGET) compilé"

# ─── Compilation des .o ───────────────────────────────────────
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ─── Création du dossier build ────────────────────────────────
$(OBJDIR):
	mkdir -p $(OBJDIR)

# ─── Nettoyage ────────────────────────────────────────────────
clean:
	rm -rf $(OBJDIR) $(TARGET)
	@echo "✓  nettoyé"

# ─── Recompilation complète ───────────────────────────────────
re: clean all

# ─── Tests unitaires ──────────────────────────────────────────
# test_infra : logique pure, pas de ncurses
# test_history, test_vterm : tests/ncurses.h stub via -Itests/
TESTDIR   = tests
# Pas de -pedantic pour les tests (stubs inline variadiques)
CFTEST    = $(filter-out -pedantic,$(CFLAGS)) -I$(TESTDIR) -I$(SRCDIR)

TEST_BINS = $(OBJDIR)/test_infra \
            $(OBJDIR)/test_history \
            $(OBJDIR)/test_vterm

.PHONY: test
test: $(OBJDIR) $(TEST_BINS)
	@echo "────────────────────────────────────────"
	@failed=0; \
	for t in $(TEST_BINS); do \
	  echo; $$t; r=$$?; \
	  echo "────────────────────────────────────────"; \
	  [ $$r -ne 0 ] && failed=$$((failed+1)); \
	done; \
	if [ $$failed -eq 0 ]; \
	  then echo "✓  Toutes les suites passées"; \
	  else echo "✗  $$failed suite(s) en echec"; exit 1; fi

$(OBJDIR)/test_infra: $(TESTDIR)/test_infra.c $(SRCDIR)/infra.c
	$(CC) $(CFLAGS) -I$(SRCDIR) $^ -o $@

$(OBJDIR)/test_history: $(TESTDIR)/test_history.c $(SRCDIR)/history.c
	$(CC) $(CFTEST) $^ -o $@

$(OBJDIR)/test_vterm: $(TESTDIR)/test_vterm.c $(SRCDIR)/vterm.c
	$(CC) $(CFTEST) $^ -o $@
