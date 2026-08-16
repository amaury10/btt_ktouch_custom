#include "moonraker_pc.h"

#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "cJSON.h"
#include "etat_klipper.h"
#include "moonraker_parse.h"
#include "moonraker_rpc.h"

/* Chemins interroges -- memes noms d'objets que MOONRAKER_CHEMIN_INTERROGATION
 * dans backend_moonraker.c, pour que moonraker_parse_status() recoive
 * exactement la meme forme de reponse des deux cotes (ESP et PC). */
#define MOONRAKER_PC_CHEMIN_STATUT "printer/objects/query?extruder&heater_bed&print_stats&virtual_sdcard&webhooks"
#define MOONRAKER_PC_CHEMIN_MACROS "printer/objects/list"

/* Tampon de reponse HTTP : statique, comme cote ESP (voir le commentaire de
 * g_tampon_reponse dans backend_moonraker.c) -- sur PC rien n'impose cette
 * discipline (pas de tas embarque a menager), mais la garder evite un aller
 * malloc()/free() par cycle pour un pur exercice de demonstration, et garde
 * ce fichier lisible cote a cote avec son homologue ESP. 8 Kio plutot que
 * 4 Kio : /printer/objects/list sur une machine avec de nombreuses macros
 * (voir vkp, ~40 objets) reste large mais y tient sans marge serree. */
#define MOONRAKER_PC_TAMPON_OCTETS 8192
static char g_tampon[MOONRAKER_PC_TAMPON_OCTETS];

static backend_hote_t g_hote;
static bool           g_hote_definie = false;
static bool           g_actif = false;

void moonraker_pc_definir_hote(const backend_hote_t *hote)
{
    if (hote == NULL) {
        return;
    }
    g_hote = *hote;
    g_hote_definie = true;
}

/* Ouvre une connexion TCP vers g_hote (adresse+port), sans resolution DNS
 * particuliere au-dela de ce que getaddrinfo() fait deja (accepte aussi bien
 * une IP litterale qu'un nom d'hote). Rend le descripteur ouvert dans
 * `*socket_out` et true en cas de succes ; ne touche pas `*socket_out` sinon. */
static bool moonraker_pc_connecter(int *socket_out)
{
    char port_texte[6];
    snprintf(port_texte, sizeof(port_texte), "%u", (unsigned)g_hote.port);

    struct addrinfo indices;
    memset(&indices, 0, sizeof(indices));
    indices.ai_family = AF_UNSPEC;
    indices.ai_socktype = SOCK_STREAM;

    struct addrinfo *resultats = NULL;
    if (getaddrinfo(g_hote.adresse, port_texte, &indices, &resultats) != 0 || resultats == NULL) {
        return false;
    }

    int fd = -1;
    for (struct addrinfo *courant = resultats; courant != NULL; courant = courant->ai_next) {
        fd = socket(courant->ai_family, courant->ai_socktype, courant->ai_protocol);
        if (fd < 0) {
            continue;
        }
        /* Delai borne par operation (connexion ET lecture) : sans lui, un
         * hote injoignable ou qui degoutte sa reponse bloquerait ce fichier
         * indefiniment -- meme raison que MOONRAKER_DELAI_MS cote ESP, valeur
         * un peu plus genereuse ici (un simulateur PC n'a pas la meme
         * contrainte de reactivite qu'un ecran tactile embarque). */
        struct timeval delai;
        delai.tv_sec = 3;
        delai.tv_usec = 0;
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &delai, sizeof(delai));
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &delai, sizeof(delai));

        if (connect(fd, courant->ai_addr, courant->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(resultats);

    if (fd < 0) {
        return false;
    }
    *socket_out = fd;
    return true;
}

/* Emet une requete GET ou POST HTTP/1.0 minimale (pas de garde-en-vie,
 * "Connection: close" -- c'est ce qui permet de lire jusqu'a la fermeture du
 * socket par le serveur SANS avoir a interpreter Content-Length ni un
 * eventuel transfert par blocs, la limite volontaire documentee dans le
 * brief de la tache : "HTTP/1.0, pas de keep-alive"). Rend le CORPS de la
 * reponse (pointeur dans g_tampon, PAS une copie) dans `*corps` et sa
 * longueur dans `*longueur`, seulement si le serveur a rendu un statut 2xx.
 * `*corps`/`*longueur` ne sont pas modifies en cas d'echec. */
static bool moonraker_pc_requete(const char *methode, const char *chemin,
                                  const char **corps, size_t *longueur)
{
    int fd;
    if (!moonraker_pc_connecter(&fd)) {
        return false;
    }

    char requete[256];
    int ecrit = snprintf(requete, sizeof(requete),
                          "%s /%s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
                          methode, chemin, g_hote.adresse, (unsigned)g_hote.port);
    if (ecrit < 0 || (size_t)ecrit >= sizeof(requete)) {
        close(fd);
        return false;
    }

    size_t envoye_total = 0;
    while (envoye_total < (size_t)ecrit) {
        ssize_t envoye = send(fd, requete + envoye_total, (size_t)ecrit - envoye_total, 0);
        if (envoye <= 0) {
            close(fd);
            return false;
        }
        envoye_total += (size_t)envoye;
    }

    size_t total = 0;
    for (;;) {
        if (total >= sizeof(g_tampon) - 1) {
            break; /* tampon plein : ce qui suit est ignore, voir le tampon MOONRAKER_TAMPON_OCTETS */
        }
        ssize_t lu = recv(fd, g_tampon + total, sizeof(g_tampon) - 1 - total, 0);
        if (lu <= 0) {
            break; /* 0 = fermeture propre du serveur (attendue, Connection: close) ; <0 = erreur/delai */
        }
        total += (size_t)lu;
    }
    close(fd);
    g_tampon[total] = '\0';

    /* Ligne de statut : "HTTP/1.x <code> <texte>\r\n". strtol sur le premier
     * jeton apres le premier espace, plutot qu'un decoupage rigide sur des
     * offsets fixes -- "HTTP/1.0" et "HTTP/1.1" n'ont pas la meme longueur
     * de prefixe que "HTTP/1.x" generique, cette lecture s'accommode des
     * deux sans distinction. */
    if (strncmp(g_tampon, "HTTP/1.", 7) != 0) {
        return false;
    }
    const char *espace = strchr(g_tampon, ' ');
    if (espace == NULL) {
        return false;
    }
    long code = strtol(espace + 1, NULL, 10);
    if (code < 200 || code >= 300) {
        return false;
    }

    const char *separateur = strstr(g_tampon, "\r\n\r\n");
    if (separateur == NULL) {
        return false;
    }
    const char *debut_corps = separateur + 4;
    size_t longueur_corps = total - (size_t)(debut_corps - g_tampon);
    if (longueur_corps == 0) {
        return false;
    }

    *corps = debut_corps;
    *longueur = longueur_corps;
    return true;
}

static esp_err_t moonraker_pc_demarrer(void *etat, const backend_hote_t *hote)
{
    /* `hote` vaut TOUJOURS NULL ici -- voir le commentaire de tete de
     * moonraker_pc.h et celui de source_etat_sim_demarrer() (elle n'appelle
     * jamais demarrer() avec un hote reel cote simulateur). L'hote effectif
     * vient de moonraker_pc_definir_hote(), appelee par main.c avant
     * source_etat_sim_demarrer(). */
    (void)hote;

    memset(etat, 0, sizeof(etat_klipper_t));

    if (!g_hote_definie || g_hote.adresse[0] == '\0') {
        fprintf(stderr, "moonraker_pc : aucun hote defini (moonraker_pc_definir_hote jamais appelee)\n");
        return ESP_ERR_INVALID_ARG;
    }

    g_actif = true;
    printf("moonraker_pc : demarrage (hote=%s port=%u)\n", g_hote.adresse, (unsigned)g_hote.port);
    return ESP_OK;
}

static esp_err_t moonraker_pc_rafraichir(void *etat)
{
    if (!g_actif) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *corps;
    size_t      longueur;
    if (!moonraker_pc_requete("GET", MOONRAKER_PC_CHEMIN_STATUT, &corps, &longueur)) {
        return ESP_FAIL;
    }
    if (!moonraker_parse_status(corps, longueur, (etat_klipper_t *)etat)) {
        fprintf(stderr, "moonraker_pc : reponse de statut inexploitable\n");
        return ESP_FAIL;
    }

    /* Best-effort : la liste des macros n'engage jamais le succes du cycle
     * (voir le commentaire de tete de ce fichier) -- une machine reelle sans
     * macro accessible, ou un second GET qui echoue isolement, ne doit pas
     * griser l'ecran alors que le statut, lui, vient d'etre lu avec succes. */
    if (moonraker_pc_requete("GET", MOONRAKER_PC_CHEMIN_MACROS, &corps, &longueur)) {
        (void)rpc_lire_macros((etat_klipper_t *)etat, corps, longueur);
    }

    return ESP_OK;
}

static void moonraker_pc_arreter(void *etat)
{
    (void)etat;
    g_actif = false;
    printf("moonraker_pc : arret\n");
}

/* Extrait "nom" de arguments_json = {"nom":"<macro>"} -- copie volontaire de
 * moonraker_extraire_nom_macro() (backend_moonraker.c) : meme forme d'entree
 * (ecran_macros.c, voir ecran_macros_construire_arguments()), meme raison de
 * passer par cJSON plutot qu'un strstr borne (tolerance aux espaces/ordre
 * des cles). Pas partagee entre les deux fichiers : chacun est static a son
 * propre fichier, une indirection commune pour deux appelants ESP/PC qui ne
 * se recouvrent jamais a l'execution n'apporterait rien ici. */
static bool moonraker_pc_extraire_nom_macro(const char *arguments_json, char *sortie, size_t taille)
{
    if (arguments_json == NULL || sortie == NULL || taille == 0) {
        return false;
    }
    cJSON *racine = cJSON_Parse(arguments_json);
    if (racine == NULL) {
        return false;
    }
    const cJSON *nom = cJSON_GetObjectItemCaseSensitive(racine, "nom");
    bool ok = cJSON_IsString(nom) && nom->valuestring != NULL;
    if (ok) {
        int ecrit = snprintf(sortie, taille, "%s", nom->valuestring);
        ok = (ecrit >= 0) && ((size_t)ecrit < taille);
    }
    cJSON_Delete(racine);
    return ok;
}

/* Commande : repli HTTP uniquement, meme forme que le chemin HTTP (pas WS)
 * de backend_moonraker_commande() -- POST /printer/gcode/script?script=<nom>
 * pour une macro, POST /<chemin> (moonraker_chemin_commande()) pour les
 * quatre actions communes. Reutilise volontairement moonraker_chemin_commande()
 * (moonraker_parse.c, fonction pure deja liee ici pour moonraker_parse_status()) --
 * une seule table action -> chemin, jamais une deuxieme copie. */
static esp_err_t moonraker_pc_commande(void *etat, const char *action, const char *arguments_json)
{
    (void)etat;

    if (!g_actif) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *corps;
    size_t      longueur;

    if (strcmp(action, BACKEND_ACTION_MACRO) == 0) {
        char nom_macro[KLIPPER_MACRO_NOM_MAX];
        if (!moonraker_pc_extraire_nom_macro(arguments_json, nom_macro, sizeof(nom_macro))) {
            fprintf(stderr, "moonraker_pc : commande macro sans nom exploitable\n");
            return ESP_ERR_NOT_SUPPORTED;
        }
        char chemin[KLIPPER_MACRO_NOM_MAX + 32];
        int  ecrit = snprintf(chemin, sizeof(chemin), "printer/gcode/script?script=%s", nom_macro);
        if (ecrit < 0 || (size_t)ecrit >= sizeof(chemin)) {
            return ESP_FAIL;
        }
        return moonraker_pc_requete("POST", chemin, &corps, &longueur) ? ESP_OK : ESP_FAIL;
    }

    char chemin[MOONRAKER_CHEMIN_COMMANDE_MAX];
    if (!moonraker_chemin_commande(action, chemin, sizeof(chemin))) {
        fprintf(stderr, "moonraker_pc : commande inconnue %s\n", action);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return moonraker_pc_requete("POST", chemin, &corps, &longueur) ? ESP_OK : ESP_FAIL;
}

/* Cadence fixe, 1000 ms : pas de WS cote PC (voir le commentaire de tete de
 * ce fichier), donc pas de la double cadence 250/1000 ms de
 * backend_moonraker_periode_ms() -- NULL suffirait (le socle retombe alors
 * sur BOUCLE_PERIODE_MS_DEFAUT, qui vaut deja 1000, voir core/boucle_cycle.h),
 * mais l'expliciter ici documente le choix plutot que de le laisser
 * implicite dans une valeur par defaut lue ailleurs. */
static uint32_t moonraker_pc_periode_ms(void *etat)
{
    (void)etat;
    return 1000u;
}

static const backend_desc_t g_moonraker_pc_desc = {
    .nom = "moonraker-pc",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = moonraker_pc_demarrer,
    .rafraichir = moonraker_pc_rafraichir,
    .arreter = moonraker_pc_arreter,
    .commande = moonraker_pc_commande,
    .periode_ms = moonraker_pc_periode_ms,
};

const backend_desc_t *moonraker_pc_desc(void)
{
    return &g_moonraker_pc_desc;
}
