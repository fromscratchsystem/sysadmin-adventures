#include "defs.h"
#include "ui.h"
#include "history.h"
#include "shell.h"
#include "container.h"
#include "vterm.h"
#include "infra.h"

#include <errno.h>
#include <stdio.h>
#include <sys/select.h>

/* ═══════════════════════════════════════════════════════════════
 * SIGNAUX
 * ═══════════════════════════════════════════════════════════════ */

static volatile sig_atomic_t g_resize_pending = 0;

static void handle_sigwinch(int sig) { (void)sig; g_resize_pending = 1; }

/* ═══════════════════════════════════════════════════════════════
 * NARRATEUR — réactions aux commandes
 * ═══════════════════════════════════════════════════════════════ */

/* Retourne 1 si la commande est bloquee, 0 si elle doit etre executee. */
static int narrator_react(Panel *narrator, const char *cmd) {
	/* ── Commandes bloquées ── */

	/* rm ciblant la racine : "rm ... /" ou "rm ... /STAR" */
	if (strncmp(cmd, "rm ", 3) == 0) {
		const char *sp = strstr(cmd, " /");
		if (sp && (sp[2] == '\0' || sp[2] == ' ' || sp[2] == '*')) {
			narrator_say(narrator, "Supprimer la racine. Classique.");
			narrator_say(narrator, "Non. Cible un chemin precis, pas '/'.");
			return 1;
		}
	}

	/* Fork bomb canonique */
	if (strstr(cmd, ":(){ :|:& };:") != NULL) {
		narrator_say(narrator, "Une fork bomb. Vraiment ? Non.");
		return 1;
	}

	/* ── Commentaires (commande executee quand meme) ── */
	if (strcmp(cmd, "ls") == 0)
		narrator_say(narrator, "Oh. Tu sais faire 'ls'. Epoustouflant.");
	else if (strcmp(cmd, "pwd") == 0)
		narrator_say(narrator, "Tu sais ou tu es. Bravo. Vraiment.");
	else if (strncmp(cmd, "man ", 4) == 0)
		narrator_say(narrator, "Tiens, quelqu'un qui lit la doc. Presque fier.");
	else if (strncmp(cmd, "sudo", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' '))
		narrator_say(narrator, "Tu n'es pas root. Tu ne seras jamais root. Accepte-le.");
	else if (strcmp(cmd, "help") == 0)
		narrator_say(narrator, "RTFM. C'est tout l'aide dont tu as besoin.");

	return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * UTILITAIRES MULTI-SHELL
 * ═══════════════════════════════════════════════════════════════ */

/*
 * Ouvre un shell SSH vers un conteneur déjà en cours d'exécution.
 * Retourne le nouvel index ou -1 si MAX_SHELLS atteint.
 */
static int attach_shell(Shell *shells, int *nshells,
		const char *name, int port,
		int rows, int cols)
{
	if (*nshells >= MAX_SHELLS) return -1;
	int idx = *nshells;
	shells[idx] = shell_spawn(rows, cols, name, port);
	if (!shells[idx].alive) return -1;
	(*nshells)++;
	return idx;
}

/*
 * Déploie un nouveau conteneur puis ouvre un shell SSH vers lui.
 * Retourne le nouvel index ou -1 en cas d'échec.
 */
static int game_deploy(Shell *shells, int *nshells,
		const char *name, const char *image,
		const char **extra_nets, int nnets,
		int rows, int cols)
{
	if (*nshells >= MAX_SHELLS) return -1;
	int port = CONTAINER_SSH_PORT + *nshells;
	if (container_deploy(name, image, port, extra_nets, nnets) != 0) return -1;
	int idx = attach_shell(shells, nshells, name, port, rows, cols);
	if (idx >= 0) {
		shells[idx].nnets = nnets < MAX_NETS ? nnets : MAX_NETS;
		for (int i = 0; i < shells[idx].nnets; i++)
			strncpy(shells[idx].extra_nets[i], extra_nets[i], 31);
	}
	return idx;
}

/* ═══════════════════════════════════════════════════════════════
 * PERSISTANCE
 * ═══════════════════════════════════════════════════════════════ */

static const char *infra_path(void) {
    static char path[256];
    const char *home = getenv("HOME");
    snprintf(path, sizeof(path), "%s/.sysadmin-game.infra", home ? home : ".");
    return path;
}

/* Affiche un buffer de lignes dans le panel narrateur. */
static void narrate(Panel *p, char lines[][128], int n) {
    for (int i = 0; i < n; i++) narrator_say(p, lines[i]);
}

static const char *state_path(void) {
    static char path[256];
    const char *home = getenv("HOME");
    snprintf(path, sizeof(path), "%s/.sysadmin-game.state", home ? home : ".");
    return path;
}

static void state_save(Shell *shells, int nshells) {
    FILE *f = fopen(state_path(), "w");
    if (!f) return;
    for (int i = 1; i < nshells; i++) {   /* 0 = conteneur principal, toujours recréé */
        if (!shells[i].alive) continue;
        fprintf(f, "C:%s:%d:", shells[i].name, shells[i].port);
        for (int j = 0; j < shells[i].nnets; j++) {
            if (j > 0) fprintf(f, ",");
            fprintf(f, "%s", shells[i].extra_nets[j]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

static void state_load(Shell *shells, int *nshells,
                       int rows, int cols, Panel *narrator)
{
    FILE *f = fopen(state_path(), "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f) && *nshells < MAX_SHELLS) {
        if (line[0] != 'C' || line[1] != ':') continue;
        char name[32]; int port; char nets_buf[256] = "";
        int n = sscanf(line + 2, "%31[^:]:%d:%255[^\n]", name, &port, nets_buf);
        if (n < 2 || !container_is_running(name)) continue;

        char msg[64];
        snprintf(msg, sizeof(msg), "Reconnexion a '%s'...", name);
        narrator_say(narrator, msg);

        int idx = attach_shell(shells, nshells, name, port, rows, cols);
        if (idx < 0) {
            char fail_msg[64];
            snprintf(fail_msg, sizeof(fail_msg),
                "Echec de reconnexion a '%s'.", name);
            narrator_say(narrator, fail_msg);
            continue;
        }

        /* Restaure les réseaux additionnels */
        if (nets_buf[0]) {
            char *tok = strtok(nets_buf, ",");
            while (tok && shells[idx].nnets < MAX_NETS) {
                strncpy(shells[idx].extra_nets[shells[idx].nnets++], tok, 31);
                tok = strtok(NULL, ",");
            }
        }
    }
    fclose(f);
}

/* ═══════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════ */

int main(void) {
	/* Tout ce qui appelle system() doit s'exécuter AVANT initscr() */

	if (container_init_network() != 0) {
		fprintf(stderr, "Impossible de créer le réseau Podman. Abandon.\n");
		return 1;
	}
	if (container_ensure_running() != 0) {
		fprintf(stderr, "Impossible de démarrer le conteneur. Abandon.\n");
		return 1;
	}

	/* Infra physique — recrée les réseaux Podman des switches (idempotent) */
	Infra infra;
	infra_load(&infra, infra_path());
	for (int i = 0; i < infra.nswitches; i++)
		container_network_create(infra.switches[i].name);

	/* Signaux — enregistrés avant initscr() */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_sigwinch;
	sigaction(SIGWINCH, &sa, NULL);

	/* UI */
	init_ncurses();
	Layout l = create_layout();

	/* Shells */
	Shell shells[MAX_SHELLS];
	int   nshells     = 0;
	int   active      = 0;

	int shell_rows = l.shell.lines - 2;
	int shell_cols = l.term_cols  - 2;

	attach_shell(shells, &nshells, "player@datacenter",
			CONTAINER_SSH_PORT, shell_rows, shell_cols);

	state_load(shells, &nshells, shell_rows, shell_cols, &l.narrator);

	/* Historique */
	History h;
	memset(&h, 0, sizeof(h));

	/* État initial */
	draw_status(l.status, l.term_cols, "2031-03-14 03:47", 99.2f, 3);
	draw_tabs(l.tab_bar, shells, nshells, active, l.term_cols);
	draw_prompt(l.input_win);

	narrator_say(&l.narrator, "Bien. Tu existes. Felicitations.");
	narrator_say(&l.narrator, "C'est probablement ton seul succes de la journee.");
	narrator_say(&l.narrator, "Tu geres un datacenter. Essaie de ne pas tout faire planter.");
	narrator_say(&l.narrator, "Commence par taper 'ls'. Si t'es capable.");

	/* Buffer de saisie */
	char input_buf[CMD_MAX] = "";
	char saved_buf[CMD_MAX] = "";
	int  input_pos = 0;
	int  running   = 1;

	/* ═══════════════════════════════════════════════════════════
	 * Boucle principale
	 * ═══════════════════════════════════════════════════════════ */
	while (running) {

		/* ── Signaux en attente ── */
		if (g_resize_pending) {
			g_resize_pending = 0;
			l = handle_resize(&l, shells, nshells);
			shell_rows = l.shell.lines - 2;
			shell_cols = l.term_cols   - 2;
			draw_status(l.status, l.term_cols, "2031-03-14 03:47", 99.2f, 3);
			draw_tabs(l.tab_bar, shells, nshells, active, l.term_cols);
			narrator_say(&l.narrator, "[terminal redimensionne]");
			/* Re-rend le VTerm du shell actif */
			if (shells[active].alive)
				vterm_render(shells[active].vterm, l.shell.inner);
			redraw_input(l.input_win, input_buf, input_pos);
			continue;
		}

		/* ── select() : surveille stdin + tous les sockets SSH actifs ── */
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		int nfds = STDIN_FILENO + 1;

		for (int i = 0; i < nshells; i++) {
			if (shells[i].alive) {
				FD_SET(shells[i].sock, &rfds);
				if (shells[i].sock + 1 > nfds)
					nfds = shells[i].sock + 1;
			}
		}

		struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
		int ret = select(nfds, &rfds, NULL, NULL, &tv);
		if (ret < 0) {
			if (errno == EINTR) continue;
			break;
		}

		/* ── Sortie SSH (tous les shells) ── */
		int need_tab_refresh = 0;
		for (int i = 0; i < nshells; i++) {
			if (!shells[i].alive) continue;
			if (!FD_ISSET(shells[i].sock, &rfds)) continue;

			WINDOW *target = (i == active) ? l.shell.inner : NULL;
			shell_read_output(&shells[i], target);

			if (!shells[i].alive) {
				narrator_say(&l.narrator, "Connexion au conteneur perdue.");
				/* Si c'était le dernier shell, on quitte */
				int any_alive = 0;
				for (int j = 0; j < nshells; j++)
					if (shells[j].alive) { any_alive = 1; break; }
				if (!any_alive) { running = 0; break; }
				/* Sinon, switcher vers le premier shell vivant */
				if (i == active) {
					for (int j = 0; j < nshells; j++) {
						if (shells[j].alive) { active = j; break; }
					}
					vterm_render(shells[active].vterm, l.shell.inner);
				}
				need_tab_refresh = 1;
			} else if (i != active) {
				/* Données reçues hors focus → marquer l'onglet */
				shells[i].has_activity = 1;
				need_tab_refresh = 1;
			}
		}
		if (!running) break;

		if (need_tab_refresh) {
			draw_tabs(l.tab_bar, shells, nshells, active, l.term_cols);
		}

		if (shells[active].alive) {
			/* Remet le curseur sur la ligne de saisie */
			wmove(l.input_win, 0, 2 + input_pos);
			wrefresh(l.input_win);
		}

		/* ── Clavier ── */
		if (!FD_ISSET(STDIN_FILENO, &rfds)) continue;

		wtimeout(l.input_win, 0);
		int ch = wgetch(l.input_win);
		wtimeout(l.input_win, -1);
		if (ch == ERR) continue;

		/* ── Touches de navigation entre onglets (F1-F8) ── */
		if (ch >= KEY_F(1) && ch <= KEY_F(MAX_SHELLS)) {
			int target = ch - KEY_F(1);
			if (target < nshells && shells[target].alive && target != active) {
				active = target;
				shells[active].has_activity = 0;
				draw_tabs(l.tab_bar, shells, nshells, active, l.term_cols);
				vterm_render(shells[active].vterm, l.shell.inner);
				redraw_input(l.input_win, input_buf, input_pos);
			}
			continue;
		}

		/* ── Scrollback PageUp / PageDown ── */
		if (ch == KEY_PPAGE || ch == KEY_NPAGE) {
			if (shells[active].alive && shells[active].vterm) {
				int half = shells[active].vterm->rows / 2;
				vterm_scroll(shells[active].vterm,
				             ch == KEY_PPAGE ? half : -half);
				vterm_render(shells[active].vterm, l.shell.inner);
				redraw_input(l.input_win, input_buf, input_pos);
			}
			continue;
		}

		/* Toute autre touche ramène à la vue temps-réel */
		if (shells[active].alive && shells[active].vterm
				&& shells[active].vterm->sb_offset != 0) {
			shells[active].vterm->sb_offset = 0;
			vterm_render(shells[active].vterm, l.shell.inner);
		}

		switch (ch) {

			case '\n':
			case KEY_ENTER:
				input_buf[input_pos] = '\0';
				if (strlen(input_buf) == 0) break;

				history_push(&h, input_buf);

				if (input_buf[0] == '/') {
					/* ══════════════════════════════════════════════
					 * Commandes du jeu (préfixe /)
					 * ══════════════════════════════════════════════ */
					const char *gc = input_buf + 1;   /* après le '/' */

					if (strcmp(gc, "exit") == 0 || strcmp(gc, "quit") == 0) {
						running = 0;

					} else if (strncmp(gc, "network ", 8) == 0) {
						/* /network create|delete <nom> */
						const char *sub = gc + 8;
						char net_name[32] = "";
						if (strncmp(sub, "create ", 7) == 0) {
							sscanf(sub + 7, "%31s", net_name);
							if (net_name[0] == '\0') {
								narrator_say(&l.narrator, "Usage : /network create <nom>");
							} else if (container_network_create(net_name) == 0) {
								char msg[64];
								snprintf(msg, sizeof(msg), "Reseau '%s' cree.", net_name);
								narrator_say(&l.narrator, msg);
							} else {
								narrator_say(&l.narrator, "Echec de la creation du reseau.");
							}
						} else if (strncmp(sub, "delete ", 7) == 0) {
							sscanf(sub + 7, "%31s", net_name);
							if (net_name[0] == '\0') {
								narrator_say(&l.narrator, "Usage : /network delete <nom>");
							} else if (container_network_delete(net_name) == 0) {
								char msg[64];
								snprintf(msg, sizeof(msg), "Reseau '%s' supprime.", net_name);
								narrator_say(&l.narrator, msg);
							} else {
								narrator_say(&l.narrator, "Echec de la suppression du reseau.");
							}
						} else {
							narrator_say(&l.narrator,
								"Usage : /network create|delete <nom>");
						}

					} else if (strncmp(gc, "deploy ", 7) == 0) {
						/* /deploy <nom> [<image>] [--networks net1,net2] */
						char dep_name[32]      = "";
						char dep_image[128]    = CONTAINER_IMAGE;
						char dep_nets_buf[256] = "";
						const char *dep_net_ptrs[MAX_NETS];
						int  dep_nnets         = 0;

						char tmp[CMD_MAX];
						strncpy(tmp, gc + 7, CMD_MAX - 1);
						tmp[CMD_MAX - 1] = '\0';
						char *tok = strtok(tmp, " ");
						if (tok) { strncpy(dep_name, tok, 31); tok = strtok(NULL, " "); }
						while (tok) {
							if (strcmp(tok, "--networks") == 0) {
								char *nxt = strtok(NULL, " ");
								if (nxt) strncpy(dep_nets_buf, nxt, 255);
							} else if (tok[0] != '-') {
								strncpy(dep_image, tok, 127);
							}
							tok = strtok(NULL, " ");
						}
						if (dep_nets_buf[0]) {
							char *p = strtok(dep_nets_buf, ",");
							while (p && dep_nnets < MAX_NETS) {
								dep_net_ptrs[dep_nnets++] = p;
								p = strtok(NULL, ",");
							}
						}

						if (dep_name[0] == '\0') {
							narrator_say(&l.narrator,
								"Usage : /deploy <nom> [image] [--networks net1,net2]");
						} else if (nshells >= MAX_SHELLS) {
							narrator_say(&l.narrator,
								"Limite de conteneurs atteinte (8 max).");
						} else {
							char msg[64];
							snprintf(msg, sizeof(msg),
								"Deploiement de '%s' en cours...", dep_name);
							narrator_say(&l.narrator, msg);
							doupdate();

							int idx = game_deploy(shells, &nshells, dep_name, dep_image,
								dep_net_ptrs, dep_nnets, shell_rows, shell_cols);
							if (idx >= 0) {
								snprintf(msg, sizeof(msg),
									"Operationnel. F%d pour y acceder.", idx + 1);
								narrator_say(&l.narrator, msg);
								draw_tabs(l.tab_bar, shells, nshells, active, l.term_cols);
								state_save(shells, nshells);
							} else {
								snprintf(msg, sizeof(msg),
									"Echec du deploiement de '%s'.", dep_name);
								narrator_say(&l.narrator, msg);
							}
						}

					} else if (strncmp(gc, "stop ", 5) == 0) {
						/* /stop <nom> */
						char stop_name[32] = "";
						sscanf(gc + 5, "%31s", stop_name);
						if (stop_name[0] == '\0') {
							narrator_say(&l.narrator, "Usage : /stop <nom>");
						} else {
							int found = -1;
							for (int i = 0; i < nshells; i++) {
								if (strcmp(shells[i].name, stop_name) == 0) {
									found = i; break;
								}
							}
							if (found < 0) {
								char msg[64];
								snprintf(msg, sizeof(msg),
									"Aucun conteneur '%s' connu.", stop_name);
								narrator_say(&l.narrator, msg);
							} else {
								shell_close(&shells[found]);
								container_stop(stop_name);
								char msg[64];
								snprintf(msg, sizeof(msg),
									"Conteneur '%s' arrete.", stop_name);
								narrator_say(&l.narrator, msg);
								if (found == active) {
									int any = 0;
									for (int j = 0; j < nshells; j++) {
										if (shells[j].alive) {
											active = j; any = 1; break;
										}
									}
									if (!any) { running = 0; break; }
									vterm_render(shells[active].vterm, l.shell.inner);
								}
								draw_tabs(l.tab_bar, shells, nshells, active, l.term_cols);
								state_save(shells, nshells);
							}
						}

					} else if (strncmp(gc, "rack", 4) == 0
						&& (gc[4] == ' ' || gc[4] == '\0')) {
					/* ── /rack create|list|show ── */
					const char *sub = gc[4] == ' ' ? gc + 5 : "";
					char lines[64][128]; int nl = 0;

					if (strncmp(sub, "create ", 7) == 0) {
						char rname[32] = ""; int units = RACK_DEFAULT_U;
						sscanf(sub + 7, "%31s %d", rname, &units);
						if (rname[0] == '\0') {
							narrator_say(&l.narrator,
								"Usage : /rack create <nom> [<units>]");
						} else {
							int rc = infra_rack_create(&infra, rname, units);
							if (rc == 0) {
								char msg[64];
								snprintf(msg, sizeof(msg),
									"Baie '%s' (%dU) creee.", rname, units);
								narrator_say(&l.narrator, msg);
								infra_save(&infra, infra_path());
							} else if (rc == -3) {
								narrator_say(&l.narrator, "Une baie de ce nom existe deja.");
							} else {
								narrator_say(&l.narrator, "Limite de baies atteinte.");
							}
						}
					} else if (strcmp(sub, "list") == 0) {
						infra_list_racks(&infra, lines, &nl, 64);
						narrate(&l.narrator, lines, nl);
					} else if (strncmp(sub, "show ", 5) == 0) {
						char rname[32] = "";
						sscanf(sub + 5, "%31s", rname);
						infra_rack_render(&infra, rname, lines, &nl, 64);
						narrate(&l.narrator, lines, nl);
					} else {
						narrator_say(&l.narrator,
							"Usage : /rack create|list|show <nom>");
					}

				} else if (strncmp(gc, "server", 6) == 0
						&& (gc[6] == ' ' || gc[6] == '\0')) {
					/* ── /server add|poweron|poweroff|list ── */
					const char *sub = gc[6] == ' ' ? gc + 7 : "";
					char lines[64][128]; int nl = 0;

					if (strncmp(sub, "add ", 4) == 0) {
						char sname[32]="", srack[32]="";
						int slot=1, size_u=1, cpu=1, ram=2048, disk=100;
						sscanf(sub + 4, "%31s %31s %d %d %d %d %d",
							sname, srack, &slot, &size_u, &cpu, &ram, &disk);
						if (sname[0]=='\0' || srack[0]=='\0') {
							narrator_say(&l.narrator,
								"Usage : /server add <nom> <rack> <slot> [<U> <cpu> <ram> <disk>]");
						} else {
							int rc = infra_server_add(&infra, sname, srack,
								slot, size_u, cpu, ram, disk);
							char msg[128];
							if (rc == 0) {
								snprintf(msg, sizeof(msg),
									"Serveur '%s' installe en %s slot %d.", sname, srack, slot);
								narrator_say(&l.narrator, msg);
								infra_save(&infra, infra_path());
							} else if (rc == -2) {
								narrator_say(&l.narrator, "Baie inconnue.");
							} else if (rc == -3) {
								narrator_say(&l.narrator, "Nom deja utilise.");
							} else if (rc == -4) {
								narrator_say(&l.narrator, "Slot occupe.");
							} else {
								narrator_say(&l.narrator, "Limite de serveurs atteinte.");
							}
						}

					} else if (strncmp(sub, "poweron ", 8) == 0
							|| strcmp(sub, "poweron") == 0) {
						char sname[32] = "";
						sscanf(sub + (sub[7]==' ' ? 8 : 7), "%31s", sname);
						PhysServer *srv = infra_find_server(&infra, sname);
						if (!srv) {
							narrator_say(&l.narrator, "Serveur inconnu.");
						} else if (srv->powered) {
							narrator_say(&l.narrator, "Serveur deja allume.");
						} else if (nshells >= MAX_SHELLS) {
							narrator_say(&l.narrator, "Trop de sessions actives.");
						} else {
							const char *nets[MAX_NETS];
							int nnets = infra_server_nets(&infra, srv->name,
								nets, MAX_NETS);
							char msg[64];
							snprintf(msg, sizeof(msg),
								"Demarrage de '%s'...", srv->name);
							narrator_say(&l.narrator, msg);
							doupdate();
							if (container_deploy(srv->name, CONTAINER_IMAGE,
									srv->port, nets, nnets) != 0) {
								narrator_say(&l.narrator, "Echec du demarrage.");
							} else {
								int idx = attach_shell(shells, &nshells,
									srv->name, srv->port,
									shell_rows, shell_cols);
								if (idx >= 0) {
									srv->powered = 1;
									shells[idx].nnets = nnets;
									for (int i = 0; i < nnets; i++)
										strncpy(shells[idx].extra_nets[i],
											nets[i], 31);
									draw_tabs(l.tab_bar, shells, nshells,
										active, l.term_cols);
									state_save(shells, nshells);
									infra_save(&infra, infra_path());
									snprintf(msg, sizeof(msg),
										"'%s' operationnel. F%d.",
										srv->name, idx + 1);
									narrator_say(&l.narrator, msg);
								} else {
									narrator_say(&l.narrator,
										"Echec de la connexion SSH.");
								}
							}
						}

					} else if (strncmp(sub, "poweroff ", 9) == 0
							|| strcmp(sub, "poweroff") == 0) {
						char sname[32] = "";
						sscanf(sub + (sub[8]==' ' ? 9 : 8), "%31s", sname);
						PhysServer *srv = infra_find_server(&infra, sname);
						if (!srv) {
							narrator_say(&l.narrator, "Serveur inconnu.");
						} else if (!srv->powered) {
							narrator_say(&l.narrator, "Serveur deja eteint.");
						} else {
							int found = -1;
							for (int i = 0; i < nshells; i++)
								if (shells[i].alive &&
										strcmp(shells[i].name, srv->name)==0)
									{ found = i; break; }
							if (found >= 0) {
								shell_close(&shells[found]);
								if (found == active) {
									for (int j = 0; j < nshells; j++)
										if (shells[j].alive)
											{ active = j; break; }
									if (shells[active].alive)
										vterm_render(shells[active].vterm,
											l.shell.inner);
								}
								draw_tabs(l.tab_bar, shells, nshells,
									active, l.term_cols);
							}
							container_stop(srv->name);
							srv->powered = 0;
							state_save(shells, nshells);
							infra_save(&infra, infra_path());
							char msg[64];
							snprintf(msg, sizeof(msg),
								"'%s' eteint.", srv->name);
							narrator_say(&l.narrator, msg);
						}

					} else if (strncmp(sub, "list", 4) == 0) {
						char rname[32] = "";
						sscanf(sub + 4, "%31s", rname);
						infra_list_servers(&infra,
							rname[0] ? rname : NULL, lines, &nl, 64);
						narrate(&l.narrator, lines, nl);
					} else {
						narrator_say(&l.narrator,
							"Usage : /server add|poweron|poweroff|list");
					}

				} else if (strncmp(gc, "switch", 6) == 0
						&& (gc[6] == ' ' || gc[6] == '\0')) {
					/* ── /switch add|poweron|poweroff|list ── */
					const char *sub = gc[6] == ' ' ? gc + 7 : "";
					char lines[64][128]; int nl = 0;

					if (strncmp(sub, "add ", 4) == 0) {
						char swname[32]="", srack[32]="";
						int slot=1, size_u=1, ports=24;
						sscanf(sub + 4, "%31s %31s %d %d %d",
							swname, srack, &slot, &size_u, &ports);
						if (swname[0]=='\0' || srack[0]=='\0') {
							narrator_say(&l.narrator,
								"Usage : /switch add <nom> <rack> <slot> [<U> [<ports>]]");
						} else {
							int rc = infra_switch_add(&infra, swname, srack,
								slot, size_u, ports);
							char msg[80];
							if (rc == 0) {
								/* Crée le réseau Podman backing */
								container_network_create(swname);
								snprintf(msg, sizeof(msg),
									"Switch '%s' installe (%dp, reseau Podman cree).",
									swname, ports);
								narrator_say(&l.narrator, msg);
								infra_save(&infra, infra_path());
							} else if (rc == -2) {
								narrator_say(&l.narrator, "Baie inconnue.");
							} else if (rc == -3) {
								narrator_say(&l.narrator, "Nom deja utilise.");
							} else if (rc == -4) {
								narrator_say(&l.narrator, "Slot occupe.");
							} else {
								narrator_say(&l.narrator, "Limite de switches atteinte.");
							}
						}

					} else if (strncmp(sub, "poweron ", 8) == 0
							|| strcmp(sub, "poweron") == 0) {
						char swname[32] = "";
						sscanf(sub + (sub[7]==' ' ? 8 : 7), "%31s", swname);
						PhysSwitch *sw = infra_find_switch(&infra, swname);
						if (!sw) narrator_say(&l.narrator, "Switch inconnu.");
						else if (sw->powered) narrator_say(&l.narrator, "Deja allume.");
						else {
							sw->powered = 1;
							infra_save(&infra, infra_path());
							char msg[64];
							snprintf(msg, sizeof(msg), "Switch '%s' allume.", swname);
							narrator_say(&l.narrator, msg);
						}

					} else if (strncmp(sub, "poweroff ", 9) == 0
							|| strcmp(sub, "poweroff") == 0) {
						char swname[32] = "";
						sscanf(sub + (sub[8]==' ' ? 9 : 8), "%31s", swname);
						PhysSwitch *sw = infra_find_switch(&infra, swname);
						if (!sw) narrator_say(&l.narrator, "Switch inconnu.");
						else if (!sw->powered) narrator_say(&l.narrator, "Deja eteint.");
						else {
							sw->powered = 0;
							infra_save(&infra, infra_path());
							char msg[64];
							snprintf(msg, sizeof(msg), "Switch '%s' eteint.", swname);
							narrator_say(&l.narrator, msg);
						}

					} else if (strncmp(sub, "list", 4) == 0) {
						char rname[32] = "";
						sscanf(sub + 4, "%31s", rname);
						infra_list_switches(&infra,
							rname[0] ? rname : NULL, lines, &nl, 64);
						narrate(&l.narrator, lines, nl);
					} else {
						narrator_say(&l.narrator,
							"Usage : /switch add|poweron|poweroff|list");
					}

				} else if (strncmp(gc, "cable", 5) == 0
						&& (gc[5] == ' ' || gc[5] == '\0')) {
					/* ── /cable connect|list ── */
					const char *sub = gc[5] == ' ' ? gc + 6 : "";
					char lines[64][128]; int nl = 0;

					if (strncmp(sub, "connect ", 8) == 0) {
						/* /cable connect <server>:<nic> <switch>:<port> */
						char srv_nic[48]="", sw_port[48]="";
						sscanf(sub + 8, "%47s %47s", srv_nic, sw_port);
						char sname[32]="", nic[8]="", swname[32]="";
						int port = 0;
						sscanf(srv_nic,  "%31[^:]:%7s",  sname,  nic);
						sscanf(sw_port,  "%31[^:]:%d",   swname, &port);
						if (sname[0]=='\0' || nic[0]=='\0'
								|| swname[0]=='\0') {
							narrator_say(&l.narrator,
								"Usage : /cable connect <srv>:<nic> <switch>:<port>");
						} else {
							int rc = infra_cable_connect(&infra, sname, nic,
								swname, port);
							char msg[128];
							if (rc == 0) {
								snprintf(msg, sizeof(msg),
									"Cable : %s:%s -> %s:port%d",
									sname, nic, swname, port);
								narrator_say(&l.narrator, msg);
								infra_save(&infra, infra_path());
							} else if (rc == -2) {
								narrator_say(&l.narrator,
									"Serveur ou switch inconnu.");
							} else if (rc == -3) {
								narrator_say(&l.narrator,
									"Cette NIC est deja cablee.");
							} else {
								narrator_say(&l.narrator,
									"Limite de cables atteinte.");
							}
						}

					} else if (strcmp(sub, "list") == 0) {
						infra_list_cables(&infra, lines, &nl, 64);
						narrate(&l.narrator, lines, nl);
					} else {
						narrator_say(&l.narrator,
							"Usage : /cable connect|list");
					}

				} else {
						narrator_say(&l.narrator,
							"Commandes : /deploy /stop /network /rack /server /switch /cable /exit");
					}

				} else {
					/* ══════════════════════════════════════════════
					 * Commandes shell → conteneur actif
					 * ══════════════════════════════════════════════ */
					if (!narrator_react(&l.narrator, input_buf))
						shell_send_line(&shells[active], input_buf);
				}

				memset(input_buf, 0, sizeof(input_buf));
				memset(saved_buf, 0, sizeof(saved_buf));
				input_pos = 0;
				redraw_input(l.input_win, input_buf, input_pos);
				break;

			case KEY_UP:
				if (history_prev(&h, input_buf, CMD_MAX, saved_buf))
					input_pos = (int)strlen(input_buf);
				redraw_input(l.input_win, input_buf, input_pos);
				break;

			case KEY_DOWN:
				if (history_next(&h, input_buf, CMD_MAX, saved_buf))
					input_pos = (int)strlen(input_buf);
				redraw_input(l.input_win, input_buf, input_pos);
				break;

			case KEY_LEFT:
				if (input_pos > 0) {
					input_pos--;
					wmove(l.input_win, 0, 2 + input_pos);
					wrefresh(l.input_win);
				}
				break;

			case KEY_RIGHT:
				if (input_pos < (int)strlen(input_buf)) {
					input_pos++;
					wmove(l.input_win, 0, 2 + input_pos);
					wrefresh(l.input_win);
				}
				break;

			case KEY_BACKSPACE:
			case 127:
			case '\b':
				if (input_pos > 0) {
					memmove(&input_buf[input_pos - 1],
							&input_buf[input_pos],
							strlen(input_buf) - input_pos + 1);
					input_pos--;
					redraw_input(l.input_win, input_buf, input_pos);
				}
				break;

			default:
				if (ch >= 1 && ch < 32) {
					/* Caractères de contrôle (Ctrl+C, Ctrl+D, Ctrl+Z…) :
					 * transmis directement au processus dans le conteneur. */
					char raw = (char)ch;
					shell_send_raw(&shells[active], &raw, 1);
					break;
				}
				if (ch >= 32 && ch < 127 && input_pos < CMD_MAX - 1) {
					int len = (int)strlen(input_buf);
					if (len < CMD_MAX - 1) {
						memmove(&input_buf[input_pos + 1],
								&input_buf[input_pos],
								len - input_pos + 1);
						input_buf[input_pos] = (char)ch;
						input_pos++;
					}
					redraw_input(l.input_win, input_buf, input_pos);
				}
				break;
		}
	}

	/* ── Nettoyage ── */
	for (int i = 0; i < nshells; i++)
		shell_close(&shells[i]);

	destroy_layout(&l);
	endwin();
	return 0;
}
