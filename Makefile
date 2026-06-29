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
