#include "defs.h"
#include "ui.h"
#include "history.h"
#include "shell.h"
#include "container.h"
#include "vterm.h"
#include "infra.h"
#include "hardware.h"

#include <errno.h>
#include <stdio.h>
#include <sys/select.h>

/* ═══════════════════════════════════════════════════════════════
 * SIGNAUX
 * ═══════════════════════════════════════════════════════════════ */

static volatile sig_atomic_t g_resize_pending = 0;

static void handle_sigwinch(int sig) { (void)sig; g_resize_pending = 1; }

/* ═══════════════════════════════════════════════════════════════
 * NARRATEUR — réactions aux commandes shell
 * ═══════════════════════════════════════════════════════════════ */

/* Retourne 1 si la commande est bloquée (ne pas l'envoyer au shell). */
static int narrator_react(Panel *narrator, const char *cmd) {
    if (strncmp(cmd, "rm ", 3) == 0) {
        const char *sp = strstr(cmd, " /");
        if (sp && (sp[2] == '\0' || sp[2] == ' ' || sp[2] == '*')) {
            narrator_say(narrator, "Supprimer la racine. Classique.");
            narrator_say(narrator, "Non. Cible un chemin precis, pas '/'.");
            return 1;
        }
    }
    if (strstr(cmd, ":(){ :|:& };:") != NULL) {
        narrator_say(narrator, "Une fork bomb. Vraiment ? Non.");
        return 1;
    }

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

/* Contexte partagé entre les handlers de commandes jeu. */
typedef struct {
    Shell  *shells;
    int    *nshells;
    int    *active;
    int    *running;
    Layout *l;
} ShCtx;

/*
 * Retire les shells morts du tableau et réajuste *active.
 * À appeler après shell_close(), avant draw_tabs().
 */
static void shells_compact(Shell *shells, int *nshells, int *active) {
    for (int i = 0; i < *nshells; ) {
        if (shells[i].alive) { i++; continue; }
        memmove(&shells[i], &shells[i + 1],
                (size_t)(*nshells - i - 1) * sizeof(Shell));
        (*nshells)--;
        if (*active > i)              (*active)--;
        if (*active >= *nshells && *nshells > 0) *active = *nshells - 1;
    }
}

/*
 * Ouvre un shell SSH vers un conteneur déjà en cours d'exécution.
 * cname : nom Podman réel pour gérer sysadmin-net ("" = conteneur principal).
 * Retourne le nouvel index ou -1 si MAX_SHELLS atteint.
 */
static int attach_shell(Shell *shells, int *nshells,
        const char *name, const char *cname, int port, int rows, int cols)
{
    if (*nshells >= MAX_SHELLS) return -1;
    if (cname && cname[0])
        container_mgmt_connect(cname);
    int idx = *nshells;
    shells[idx] = shell_spawn(rows, cols, name, port);
    if (!shells[idx].alive) {
        if (cname && cname[0]) container_mgmt_disconnect(cname);
        return -1;
    }
    strncpy(shells[idx].container_name, cname ? cname : "", 31);
    (*nshells)++;
    return idx;
}

/*
 * Déploie un nouveau conteneur puis ouvre un shell SSH vers lui.
 * Retourne le nouvel index ou -1 en cas d'échec.
 */
static int game_deploy(Shell *shells, int *nshells,
        const char *name, const char *image,
        const char **extra_nets, int nnets, int rows, int cols)
{
    if (*nshells >= MAX_SHELLS) return -1;
    int port = CONTAINER_SSH_PORT + *nshells;
    if (container_deploy(name, image, port, extra_nets, nnets) != 0) return -1;
    int idx = attach_shell(shells, nshells, name, name, port, rows, cols);
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

static const char *state_path(void) {
    static char path[256];
    const char *home = getenv("HOME");
    snprintf(path, sizeof(path), "%s/.sysadmin-game.state", home ? home : ".");
    return path;
}

/* Affiche un buffer de lignes dans le panel narrateur. */
static void narrate(Panel *p, char lines[][128], int n) {
    for (int i = 0; i < n; i++) narrator_say(p, lines[i]);
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
        if (n < 2) continue;

        /* Parse nets before container_deploy (needed to reconnect extra networks). */
        const char *net_ptrs[MAX_NETS];
        int nnets = 0;
        char nets_copy[256];
        strncpy(nets_copy, nets_buf, sizeof(nets_copy) - 1);
        nets_copy[sizeof(nets_copy) - 1] = '\0';
        if (nets_copy[0]) {
            char *tok = strtok(nets_copy, ",");
            while (tok && nnets < MAX_NETS) { net_ptrs[nnets++] = tok; tok = strtok(NULL, ","); }
        }

        narrator_printf(narrator, "Reconnexion a '%s'...", name);
        if (container_deploy(name, CONTAINER_IMAGE, port, net_ptrs, nnets) != 0) {
            narrator_printf(narrator, "Echec de reconnexion a '%s'.", name);
            continue;
        }
        int idx = attach_shell(shells, nshells, name, name, port, rows, cols);
        if (idx < 0) {
            narrator_printf(narrator, "Echec de reconnexion a '%s'.", name);
            continue;
        }

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
 * GESTIONNAIRES DE COMMANDES JEU
 * ═══════════════════════════════════════════════════════════════ */

static void cmd_network(const char *sub, Panel *nar)
{
    char name[32] = "";
    if (strncmp(sub, "create ", 7) == 0) {
        sscanf(sub + 7, "%31s", name);
        if (!name[0]) { narrator_say(nar, "Usage : /network create <nom>"); return; }
        if (container_network_create(name) == 0)
            narrator_printf(nar, "Reseau '%s' cree.", name);
        else
            narrator_say(nar, "Echec de la creation du reseau.");
    } else if (strncmp(sub, "delete ", 7) == 0) {
        sscanf(sub + 7, "%31s", name);
        if (!name[0]) { narrator_say(nar, "Usage : /network delete <nom>"); return; }
        if (container_network_delete(name) == 0)
            narrator_printf(nar, "Reseau '%s' supprime.", name);
        else
            narrator_say(nar, "Echec de la suppression du reseau.");
    } else {
        narrator_say(nar, "Usage : /network create|delete <nom>");
    }
}

static void cmd_deploy(const char *args, Panel *nar, ShCtx *sc)
{
    char dep_name[32]      = "";
    char dep_image[128]    = CONTAINER_IMAGE;
    char dep_nets_buf[256] = "";
    const char *dep_net_ptrs[MAX_NETS];
    int  dep_nnets = 0;

    char tmp[CMD_MAX];
    strncpy(tmp, args, CMD_MAX - 1);
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
        while (p && dep_nnets < MAX_NETS) { dep_net_ptrs[dep_nnets++] = p; p = strtok(NULL, ","); }
    }

    if (!dep_name[0]) {
        narrator_say(nar, "Usage : /deploy <nom> [image] [--networks net1,net2]");
        return;
    }
    if (*sc->nshells >= MAX_SHELLS) {
        narrator_say(nar, "Limite de conteneurs atteinte (8 max).");
        return;
    }

    narrator_printf(nar, "Deploiement de '%s' en cours...", dep_name);
    doupdate();

    Layout *l = sc->l;
    int sh_rows = l->shell.lines - 2;
    int sh_cols = l->term_cols  - 2;
    int idx = game_deploy(sc->shells, sc->nshells, dep_name, dep_image,
                          dep_net_ptrs, dep_nnets, sh_rows, sh_cols);
    if (idx >= 0) {
        narrator_printf(nar, "Operationnel. F%d pour y acceder.", idx + 1);
        draw_tabs(l->tab_bar, sc->shells, *sc->nshells, *sc->active, l->term_cols);
        state_save(sc->shells, *sc->nshells);
    } else {
        narrator_printf(nar, "Echec du deploiement de '%s'.", dep_name);
    }
}

static void cmd_stop(const char *args, Panel *nar, ShCtx *sc)
{
    char name[32] = "";
    sscanf(args, "%31s", name);
    if (!name[0]) { narrator_say(nar, "Usage : /stop <nom>"); return; }

    int found = -1;
    for (int i = 0; i < *sc->nshells; i++) {
        if (strcmp(sc->shells[i].name, name) == 0) { found = i; break; }
    }
    if (found < 0) {
        narrator_printf(nar, "Aucun conteneur '%s' connu.", name);
        return;
    }

    if (sc->shells[found].container_name[0])
        container_mgmt_disconnect(sc->shells[found].container_name);
    shell_close(&sc->shells[found]);
    container_stop(name);
    narrator_printf(nar, "Conteneur '%s' arrete.", name);

    if (found == *sc->active) {
        int any = 0;
        for (int j = 0; j < *sc->nshells; j++) {
            if (sc->shells[j].alive) { *sc->active = j; any = 1; break; }
        }
        if (!any) { *sc->running = 0; return; }
    }

    shells_compact(sc->shells, sc->nshells, sc->active);
    Layout *l = sc->l;
    vterm_render(sc->shells[*sc->active].vterm, l->shell.inner);
    draw_tabs(l->tab_bar, sc->shells, *sc->nshells, *sc->active, l->term_cols);
    state_save(sc->shells, *sc->nshells);
}

static void cmd_rack(const char *sub, Infra *inf, Panel *nar)
{
    char lines[64][128]; int nl = 0;

    if (strncmp(sub, "create ", 7) == 0) {
        char rname[32] = ""; int units = RACK_DEFAULT_U;
        sscanf(sub + 7, "%31s %d", rname, &units);
        if (!rname[0]) {
            narrator_say(nar, "Usage : /rack create <nom> [<units>]");
            return;
        }
        int rc = infra_rack_create(inf, rname, units);
        if (rc == 0) {
            narrator_printf(nar, "Baie '%s' (%dU) creee.", rname, units);
            infra_save(inf, infra_path());
        } else if (rc == -3) {
            narrator_say(nar, "Une baie de ce nom existe deja.");
        } else {
            narrator_say(nar, "Limite de baies atteinte.");
        }
    } else if (strcmp(sub, "list") == 0) {
        infra_list_racks(inf, lines, &nl, 64);
        narrate(nar, lines, nl);
    } else if (strncmp(sub, "show ", 5) == 0) {
        char rname[32] = "";
        sscanf(sub + 5, "%31s", rname);
        infra_rack_render(inf, rname, lines, &nl, 64);
        narrate(nar, lines, nl);
    } else if (strncmp(sub, "delete ", 7) == 0) {
        char rname[32] = "";
        sscanf(sub + 7, "%31s", rname);
        if (!rname[0]) { narrator_say(nar, "Usage : /rack delete <nom>"); return; }
        int rc = infra_rack_delete(inf, rname);
        if (rc == 0) {
            narrator_printf(nar, "Baie '%s' supprimee.", rname);
            infra_save(inf, infra_path());
        } else if (rc == -1) {
            narrator_say(nar, "Baie inconnue.");
        } else {
            narrator_say(nar, "Baie non vide (retirez les equipements d'abord).");
        }
    } else {
        narrator_say(nar, "Usage : /rack create|list|show|delete <nom>");
    }
}

static void cmd_server(const char *sub, Infra *inf, Panel *nar, ShCtx *sc)
{
    Layout *l = sc->l;
    char lines[64][128]; int nl = 0;

    if (strcmp(sub, "models") == 0) {
        hw_list_server_models(lines, &nl, 64);
        narrate(nar, lines, nl);

    } else if (strncmp(sub, "add ", 4) == 0) {
        char sname[32]="", srack[32]="", model_arg[32]="";
        int slot=1;
        sscanf(sub + 4, "%31s %31s %d %31s", sname, srack, &slot, model_arg);
        if (!sname[0] || !srack[0]) {
            narrator_say(nar, "Usage : /server add <nom> <rack> <slot> <modele-id>");
            narrator_say(nar, "  /server models pour voir les modeles disponibles");
            return;
        }

        int rc;
        if (!model_arg[0]) {
            /* Pas de modèle : serveur générique 1U avec IPMI */
            rc = infra_server_add(inf, sname, srack, slot, 1, 1, 2048, 100);
        } else {
            const ServerModel *m = srv_model_find(model_arg);
            if (!m) {
                narrator_printf(nar, "Modele '%s' inconnu. /server models", model_arg);
                return;
            }
            if (m->size_u == 0) {
                /* Mini PC */
                rc = infra_minipc_add(inf, sname, srack, slot, model_arg);
                if (rc == -5) { narrator_say(nar, "Slot plein (2 mini PCs max par 1U)."); return; }
            } else {
                rc = infra_server_add_model(inf, sname, srack, slot,
                                            m->size_u, m->has_ipmi, model_arg);
            }
        }

        if (rc == 0) {
            PhysServer *srv = infra_find_server(inf, sname);
            if (srv) {
                hw_server_init_slots(srv, model_arg[0] ? model_arg : NULL);
                hw_recompute(srv);
            }
            narrator_printf(nar, "Serveur '%s' installe en %s slot %d.", sname, srack, slot);
            infra_save(inf, infra_path());
        } else if (rc == -2) { narrator_say(nar, "Baie inconnue.");
        } else if (rc == -3) { narrator_say(nar, "Nom deja utilise.");
        } else if (rc == -4) { narrator_say(nar, "Slot invalide (doit etre >= 1).");
        } else if (rc == -5) { narrator_say(nar, "Slot occupe.");
        } else               { narrator_say(nar, "Limite de serveurs atteinte."); }

    } else if (strncmp(sub, "poweron", 7) == 0 && (sub[7] == ' ' || sub[7] == '\0')) {
        char sname[32] = "";
        if (sub[7] == ' ') sscanf(sub + 8, "%31s", sname);
        PhysServer *srv = infra_find_server(inf, sname);
        if (!srv) {
            narrator_say(nar, "Serveur inconnu.");
        } else if (srv->powered) {
            narrator_say(nar, "Serveur deja allume.");
        } else if (*sc->nshells >= MAX_SHELLS) {
            narrator_say(nar, "Trop de sessions actives.");
        } else {
            if (!srv->has_ipmi)
                narrator_say(nar, "Attention : pas d'IPMI. Acces physique requis en cas de panne.");
            const char *nets[MAX_NETS];
            int nnets = infra_server_nets(inf, srv->name, nets, MAX_NETS);
            narrator_printf(nar, "Demarrage de '%s'...", srv->name);
            doupdate();
            int sh_rows = l->shell.lines - 2;
            int sh_cols = l->term_cols  - 2;
            if (container_deploy(srv->name, CONTAINER_IMAGE, srv->port, nets, nnets) != 0) {
                narrator_say(nar, "Echec du demarrage.");
            } else {
                int idx = attach_shell(sc->shells, sc->nshells,
                                       srv->name, srv->name, srv->port, sh_rows, sh_cols);
                if (idx >= 0) {
                    srv->powered = 1;
                    sc->shells[idx].nnets = nnets;
                    for (int i = 0; i < nnets; i++)
                        strncpy(sc->shells[idx].extra_nets[i], nets[i], 31);
                    draw_tabs(l->tab_bar, sc->shells, *sc->nshells, *sc->active, l->term_cols);
                    state_save(sc->shells, *sc->nshells);
                    infra_save(inf, infra_path());
                    narrator_printf(nar, "'%s' operationnel. F%d.", srv->name, idx + 1);
                } else {
                    narrator_say(nar, "Echec de la connexion SSH.");
                }
            }
        }

    } else if (strncmp(sub, "poweroff", 8) == 0 && (sub[8] == ' ' || sub[8] == '\0')) {
        char sname[32] = "";
        if (sub[8] == ' ') sscanf(sub + 9, "%31s", sname);
        PhysServer *srv = infra_find_server(inf, sname);
        if (!srv) {
            narrator_say(nar, "Serveur inconnu.");
        } else if (!srv->powered) {
            narrator_say(nar, "Serveur deja eteint.");
        } else {
            int found = -1;
            for (int i = 0; i < *sc->nshells; i++)
                if (sc->shells[i].alive && strcmp(sc->shells[i].name, srv->name) == 0)
                    { found = i; break; }
            if (found >= 0) {
                if (sc->shells[found].container_name[0])
                    container_mgmt_disconnect(sc->shells[found].container_name);
                shell_close(&sc->shells[found]);
                if (found == *sc->active) {
                    for (int j = 0; j < *sc->nshells; j++)
                        if (sc->shells[j].alive) { *sc->active = j; break; }
                }
                shells_compact(sc->shells, sc->nshells, sc->active);
                vterm_render(sc->shells[*sc->active].vterm, l->shell.inner);
                draw_tabs(l->tab_bar, sc->shells, *sc->nshells, *sc->active, l->term_cols);
            }
            container_stop(srv->name);
            srv->powered = 0;
            state_save(sc->shells, *sc->nshells);
            infra_save(inf, infra_path());
            narrator_printf(nar, "'%s' eteint.", srv->name);
        }

    } else if (strncmp(sub, "show ", 5) == 0) {
        char sname[32] = "";
        sscanf(sub + 5, "%31s", sname);
        if (!sname[0]) { narrator_say(nar, "Usage : /server show <nom>"); return; }
        infra_server_show(inf, sname, lines, &nl, 64);
        narrate(nar, lines, nl);

    } else if (strncmp(sub, "list", 4) == 0) {
        char rname[32] = "";
        sscanf(sub + 4, "%31s", rname);
        infra_list_servers(inf, rname[0] ? rname : NULL, lines, &nl, 64);
        narrate(nar, lines, nl);

    } else if (strncmp(sub, "delete ", 7) == 0) {
        char sname[32] = "";
        sscanf(sub + 7, "%31s", sname);
        if (!sname[0]) { narrator_say(nar, "Usage : /server delete <nom>"); return; }
        int rc = infra_server_delete(inf, sname);
        if (rc == 0) {
            narrator_printf(nar, "Serveur '%s' retire.", sname);
            infra_save(inf, infra_path());
        } else if (rc == -1) { narrator_say(nar, "Serveur inconnu.");
        } else               { narrator_say(nar, "Serveur allume. Eteignez-le d'abord."); }

    } else {
        narrator_say(nar, "Usage : /server add|poweron|poweroff|show|list|delete");
    }
}

static void cmd_switch(const char *sub, Infra *inf, Panel *nar)
{
    char lines[64][128]; int nl = 0;

    if (strncmp(sub, "add ", 4) == 0) {
        char swname[32]="", srack[32]="";
        int slot=1, size_u=1, ports=24;
        sscanf(sub + 4, "%31s %31s %d %d %d", swname, srack, &slot, &size_u, &ports);
        if (!swname[0] || !srack[0]) {
            narrator_say(nar, "Usage : /switch add <nom> <rack> <slot> [<U> [<ports>]]");
            return;
        }
        int rc = infra_switch_add(inf, swname, srack, slot, size_u, ports);
        if (rc == 0) {
            container_network_create(swname);
            narrator_printf(nar, "Switch '%s' installe (%dp, reseau Podman cree).", swname, ports);
            infra_save(inf, infra_path());
        } else if (rc == -2) { narrator_say(nar, "Baie inconnue.");
        } else if (rc == -3) { narrator_say(nar, "Nom deja utilise.");
        } else if (rc == -4) { narrator_say(nar, "Slot invalide (doit etre >= 1).");
        } else if (rc == -5) { narrator_say(nar, "Slot occupe.");
        } else               { narrator_say(nar, "Limite de switches atteinte."); }

    } else if (strncmp(sub, "poweron", 7) == 0 && (sub[7] == ' ' || sub[7] == '\0')) {
        char swname[32] = "";
        if (sub[7] == ' ') sscanf(sub + 8, "%31s", swname);
        PhysSwitch *sw = infra_find_switch(inf, swname);
        if (!sw)         { narrator_say(nar, "Switch inconnu.");
        } else if (sw->powered) { narrator_say(nar, "Deja allume.");
        } else {
            sw->powered = 1;
            infra_save(inf, infra_path());
            narrator_printf(nar, "Switch '%s' allume.", swname);
        }

    } else if (strncmp(sub, "poweroff", 8) == 0 && (sub[8] == ' ' || sub[8] == '\0')) {
        char swname[32] = "";
        if (sub[8] == ' ') sscanf(sub + 9, "%31s", swname);
        PhysSwitch *sw = infra_find_switch(inf, swname);
        if (!sw)          { narrator_say(nar, "Switch inconnu.");
        } else if (!sw->powered) { narrator_say(nar, "Deja eteint.");
        } else {
            sw->powered = 0;
            infra_save(inf, infra_path());
            narrator_printf(nar, "Switch '%s' eteint.", swname);
        }

    } else if (strncmp(sub, "show ", 5) == 0) {
        char swname[32] = "";
        sscanf(sub + 5, "%31s", swname);
        if (!swname[0]) { narrator_say(nar, "Usage : /switch show <nom>"); return; }
        infra_switch_show(inf, swname, lines, &nl, 64);
        narrate(nar, lines, nl);

    } else if (strncmp(sub, "list", 4) == 0) {
        char rname[32] = "";
        sscanf(sub + 4, "%31s", rname);
        infra_list_switches(inf, rname[0] ? rname : NULL, lines, &nl, 64);
        narrate(nar, lines, nl);

    } else if (strncmp(sub, "delete ", 7) == 0) {
        char swname[32] = "";
        sscanf(sub + 7, "%31s", swname);
        if (!swname[0]) { narrator_say(nar, "Usage : /switch delete <nom>"); return; }
        int rc = infra_switch_delete(inf, swname);
        if (rc == 0) {
            container_network_delete(swname);
            narrator_printf(nar, "Switch '%s' retire (reseau Podman supprime).", swname);
            infra_save(inf, infra_path());
        } else if (rc == -1) { narrator_say(nar, "Switch inconnu.");
        } else               { narrator_say(nar, "Switch allume. Eteignez-le d'abord."); }

    } else {
        narrator_say(nar, "Usage : /switch add|poweron|poweroff|show|list|delete");
    }
}

static void cmd_cable(const char *sub, Infra *inf, Panel *nar)
{
    char lines[64][128]; int nl = 0;

    if (strncmp(sub, "connect ", 8) == 0) {
        char srv_nic[48]="", sw_port[48]="";
        sscanf(sub + 8, "%47s %47s", srv_nic, sw_port);
        char sname[32]="", nic[8]="", swname[32]="";
        int port = 0;
        sscanf(srv_nic, "%31[^:]:%7s",  sname,  nic);
        sscanf(sw_port, "%31[^:]:%d",   swname, &port);
        if (!sname[0] || !nic[0] || !swname[0]) {
            narrator_say(nar, "Usage : /cable connect <srv>:<nic> <switch>:<port>");
            return;
        }
        int rc = infra_cable_connect(inf, sname, nic, swname, port);
        if (rc == 0) {
            narrator_printf(nar, "Cable : %s:%s -> %s:port%d", sname, nic, swname, port);
            infra_save(inf, infra_path());
        } else if (rc == -2) { narrator_say(nar, "Serveur ou switch inconnu.");
        } else if (rc == -3) { narrator_say(nar, "Cette NIC est deja cablee.");
        } else if (rc == -4) {
            PhysSwitch *psw = infra_find_switch(inf, swname);
            narrator_printf(nar, "Port invalide (1-%d).", psw ? psw->ports : 0);
        } else if (rc == -5) { narrator_say(nar, "Ce port du switch est deja occupe.");
        } else               { narrator_say(nar, "Limite de cables atteinte."); }

    } else if (strcmp(sub, "list") == 0) {
        infra_list_cables(inf, lines, &nl, 64);
        narrate(nar, lines, nl);

    } else if (strncmp(sub, "disconnect ", 11) == 0) {
        char srv_nic[48] = "";
        sscanf(sub + 11, "%47s", srv_nic);
        char sname[32] = "", nic[8] = "";
        sscanf(srv_nic, "%31[^:]:%7s", sname, nic);
        if (!sname[0] || !nic[0]) {
            narrator_say(nar, "Usage : /cable disconnect <srv>:<nic>");
            return;
        }
        int rc = infra_cable_disconnect(inf, sname, nic);
        if (rc == 0) {
            narrator_printf(nar, "Cable %s:%s deconnecte.", sname, nic);
            infra_save(inf, infra_path());
        } else {
            narrator_say(nar, "Cable introuvable.");
        }

    } else {
        narrator_say(nar, "Usage : /cable connect|disconnect|list");
    }
}

static void cmd_hardware(const char *sub, Infra *inf, Panel *nar,
                         const char *(*ipath)(void))
{
    char lines[64][128]; int nl = 0;

    if (strncmp(sub, "list", 4) == 0) {
        const char *filter = (sub[4] == ' ') ? sub + 5 : NULL;
        hw_list_catalog(filter, lines, &nl, 64);
        narrate(nar, lines, nl);

    } else if (strncmp(sub, "install ", 8) == 0) {
        char server[32] = "", comp[32] = "";
        sscanf(sub + 8, "%31s %31s", server, comp);
        if (!server[0] || !comp[0]) {
            narrator_say(nar, "Usage : /hardware install <serveur> <comp-id>");
            return;
        }
        int rc = hw_install(inf, server, comp);
        if (rc == 0) {
            narrator_printf(nar, "Composant '%s' installe sur '%s'.", comp, server);
            infra_save(inf, ipath());
        } else if (rc == -1) {
            narrator_printf(nar, "Composant '%s' inconnu. /hardware list", comp);
        } else if (rc == -2) {
            narrator_printf(nar, "Serveur '%s' inconnu.", server);
        } else if (rc == -3) {
            narrator_say(nar, "Plus de slots libres (ou CPU deja installe).");
        } else if (rc == -4) {
            narrator_say(nar, "Eteignez le serveur avant d'intervenir.");
        } else if (rc == -5) {
            narrator_say(nar, "Incompatibilite memoire : generation CPU/RAM differente.");
            narrator_say(nar, "  Ex: DDR4 RAM ne fonctionne pas avec un CPU DDR3.");
        }

    } else if (strncmp(sub, "remove ", 7) == 0) {
        char server[32] = "", comp[32] = "";
        sscanf(sub + 7, "%31s %31s", server, comp);
        if (!server[0] || !comp[0]) {
            narrator_say(nar, "Usage : /hardware remove <serveur> <comp-id>");
            return;
        }
        int rc = hw_remove(inf, server, comp);
        if (rc == 0) {
            narrator_printf(nar, "Composant '%s' retire de '%s'.", comp, server);
            infra_save(inf, ipath());
        } else if (rc == -1) {
            narrator_say(nar, "Composant non installe sur ce serveur.");
        } else if (rc == -2) {
            narrator_printf(nar, "Serveur '%s' inconnu.", server);
        } else if (rc == -4) {
            narrator_say(nar, "Eteignez le serveur avant d'intervenir.");
        }

    } else if (strncmp(sub, "show ", 5) == 0) {
        char server[32] = "";
        sscanf(sub + 5, "%31s", server);
        if (!server[0]) { narrator_say(nar, "Usage : /hardware show <serveur>"); return; }
        const PhysServer *srv = infra_find_server(inf, server);
        if (!srv) { narrator_printf(nar, "Serveur '%s' inconnu.", server); return; }
        hw_show_server(srv, lines, &nl, 64);
        narrate(nar, lines, nl);

    } else {
        narrator_say(nar, "Usage : /hardware list|install|remove|show");
        narrator_say(nar, "  /hardware list [cpu|dimm|sata|m2|u2|pcie_x16|...]");
        narrator_say(nar, "  /hardware install <srv> <comp-id>");
        narrator_say(nar, "  /hardware remove  <srv> <comp-id>");
        narrator_say(nar, "  /hardware show    <srv>");
    }
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

    /* Catalogues hardware — chargés depuis les fichiers de données */
    hw_catalog_load("data/hw_components.txt");
    server_catalog_load("data/server_models.txt");

    /* Infra physique — recrée les réseaux Podman des switches (idempotent) */
    Infra infra;
    infra_load(&infra, infra_path());
    /* Serveurs chargés depuis un fichier ancien (sans HW:) : init leurs slots */
    for (int i = 0; i < infra.nservers; i++) {
        PhysServer *s = &infra.servers[i];
        if (s->nhw_slots == 0 && s->model_id[0])
            hw_server_init_slots(s, s->model_id);
    }
    hw_recompute_all(&infra);
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
    int   nshells = 0;
    int   active  = 0;
    int   running = 1;

    attach_shell(shells, &nshells, "player@datacenter", "",
            CONTAINER_SSH_PORT, l.shell.lines - 2, l.term_cols - 2);

    state_load(shells, &nshells, l.shell.lines - 2, l.term_cols - 2, &l.narrator);

    ShCtx sc = { shells, &nshells, &active, &running, &l };

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

    /* ═══════════════════════════════════════════════════════════
     * Boucle principale
     * ═══════════════════════════════════════════════════════════ */
    while (running) {

        /* ── Signaux en attente ── */
        if (g_resize_pending) {
            g_resize_pending = 0;
            l = handle_resize(&l, shells, nshells);
            draw_status(l.status, l.term_cols, "2031-03-14 03:47", 99.2f, 3);
            draw_tabs(l.tab_bar, shells, nshells, active, l.term_cols);
            narrator_say(&l.narrator, "[terminal redimensionne]");
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
                if (shells[i].container_name[0])
                    container_mgmt_disconnect(shells[i].container_name);
                PhysServer *dead_srv = infra_find_server(&infra, shells[i].name);
                if (dead_srv && !dead_srv->has_ipmi) {
                    narrator_printf(&l.narrator,
                        "Connexion a '%s' perdue. Pas d'IPMI !", shells[i].name);
                    narrator_say(&l.narrator,
                        "  Intervention physique requise. Appelez un technicien.");
                } else {
                    narrator_say(&l.narrator, "Connexion au conteneur perdue.");
                }
                int any_alive = 0;
                for (int j = 0; j < nshells; j++)
                    if (shells[j].alive) { any_alive = 1; break; }
                if (!any_alive) { running = 0; break; }
                if (i == active) {
                    for (int j = 0; j < nshells; j++) {
                        if (shells[j].alive) { active = j; break; }
                    }
                    vterm_render(shells[active].vterm, l.shell.inner);
                }
                need_tab_refresh = 1;
            } else if (i != active) {
                shells[i].has_activity = 1;
                need_tab_refresh = 1;
            }
        }
        if (!running) break;

        if (need_tab_refresh) {
            shells_compact(shells, &nshells, &active);
            draw_tabs(l.tab_bar, shells, nshells, active, l.term_cols);
        }

        if (shells[active].alive) {
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

        /* ── Scroll narrateur Shift+PageUp / Shift+PageDown ── */
        if (ch == KEY_SPREVIOUS || ch == KEY_SNEXT) {
            int half = (l.narrator.lines - 2) / 2;
            narrator_scroll(&l.narrator, ch == KEY_SPREVIOUS ? half : -half);
            redraw_input(l.input_win, input_buf, input_pos);
            continue;
        }

        /* ── Scrollback PageUp / PageDown ── */
        if (ch == KEY_PPAGE || ch == KEY_NPAGE) {
            if (shells[active].alive && shells[active].vterm) {
                int half = shells[active].vterm->rows / 2;
                vterm_scroll(shells[active].vterm, ch == KEY_PPAGE ? half : -half);
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
                if (strlen(input_buf) == 0) break;

                history_push(&h, input_buf);

                if (input_buf[0] == '/') {
                    /* ══════════════════════════════════════════════
                     * Commandes du jeu (préfixe /)
                     * ══════════════════════════════════════════════ */
                    const char *gc = input_buf + 1;

                    if (strcmp(gc, "exit") == 0 || strcmp(gc, "quit") == 0) {
                        running = 0;
                    } else if (strncmp(gc, "network ", 8) == 0) {
                        cmd_network(gc + 8, &l.narrator);
                    } else if (strncmp(gc, "deploy ", 7) == 0) {
                        cmd_deploy(gc + 7, &l.narrator, &sc);
                    } else if (strncmp(gc, "stop ", 5) == 0) {
                        cmd_stop(gc + 5, &l.narrator, &sc);
                    } else if (strncmp(gc, "rack", 4) == 0 && (gc[4] == ' ' || gc[4] == '\0')) {
                        cmd_rack(gc[4] == ' ' ? gc + 5 : "", &infra, &l.narrator);
                    } else if (strncmp(gc, "server", 6) == 0 && (gc[6] == ' ' || gc[6] == '\0')) {
                        cmd_server(gc[6] == ' ' ? gc + 7 : "", &infra, &l.narrator, &sc);
                    } else if (strncmp(gc, "switch", 6) == 0 && (gc[6] == ' ' || gc[6] == '\0')) {
                        cmd_switch(gc[6] == ' ' ? gc + 7 : "", &infra, &l.narrator);
                    } else if (strncmp(gc, "cable", 5) == 0 && (gc[5] == ' ' || gc[5] == '\0')) {
                        cmd_cable(gc[5] == ' ' ? gc + 6 : "", &infra, &l.narrator);
                    } else if (strncmp(gc, "hardware", 8) == 0 && (gc[8] == ' ' || gc[8] == '\0')) {
                        cmd_hardware(gc[8] == ' ' ? gc + 9 : "", &infra, &l.narrator, infra_path);
                    } else {
                        narrator_say(&l.narrator,
                            "Commandes : /deploy /stop /network /rack /server /switch /cable /hardware /exit");
                        narrator_say(&l.narrator,
                            "  /hardware list|install|remove|show  pour gerer le materiel");
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
                if (running) redraw_input(l.input_win, input_buf, input_pos);
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
                    /* Caractères de contrôle (Ctrl+C, Ctrl+D…) : transmis directement. */
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
    for (int i = 0; i < nshells; i++) {
        if (shells[i].container_name[0])
            container_mgmt_disconnect(shells[i].container_name);
        shell_close(&shells[i]);
    }

    destroy_layout(&l);
    endwin();
    return 0;
}
