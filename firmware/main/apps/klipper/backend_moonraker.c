#include "backend_moonraker.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_timer.h"

#include "etat_klipper.h"
#include "journal.h"
#include "moonraker_parse.h"

/* Étiquette de journalisation : convention reprise du reste du firmware
 * (voir app_main.c, backend_factice.c), pour que /log reste lisible par
 * module. */
static const char *TAG = "backend_moonraker";

/* Chemin de l'interrogation périodique : les cinq sous-objets nécessaires à
 * remplir etat_klipper_t (voir moonraker_parse.c), ni plus ni moins — chaque
 * objet en trop est un aller-retour réseau et un tampon de réponse plus
 * grands sans raison. */
#define MOONRAKER_CHEMIN_INTERROGATION \
    "printer/objects/query?extruder&heater_bed&print_stats&virtual_sdcard&webhooks"

/* Tampon de réponse HTTP : STATIQUE et de taille FIXE, jamais alloué à chaque
 * appel. rafraichir() tourne une fois par seconde pendant des heures sur un
 * appareil qu'on ne peut pas rebrancher par câble ; une allocation dans ce
 * chemin est la façon la plus sûre de provoquer un redémarrage nocturne
 * qu'on ne saura pas déboguer (fragmentation du tas au bout de quelques
 * milliers d'appels). 4 Kio couvre largement une réponse
 * /printer/objects/query pour les cinq objets demandés ci-dessus (quelques
 * centaines d'octets en pratique) ; au-delà, la réponse est tronquée et
 * l'appel rend une erreur plutôt que de grandir le tampon. Partagé entre les
 * requêtes GET et POST : les deux ne s'exécutent jamais en même temps, elles
 * passent toutes deux par la même tâche unique (voir core/boucle.c).
 *
 * Ce tampon reste le SEUL stockage variable de ce fichier qui touche le tas :
 * voir g_client ci-dessous pour ce qui a changé depuis la première version
 * (revue de la tâche 8, tour 1) — le client HTTP lui-même n'est plus recréé
 * à chaque cycle. */
#define MOONRAKER_TAMPON_OCTETS 4096
static char g_tampon_reponse[MOONRAKER_TAMPON_OCTETS];

/* Taille de tampon d'URL : "http://" (7) + adresse (BACKEND_HOTE_LONGUEUR_MAX)
 * + ":" (1) + port (5 chiffres max) + "/" (1) + le plus long chemin utilisé
 * ici (l'interrogation, ~74 octets) + le nul terminal. Marge ronde
 * au-dessus de cette somme plutôt qu'un calcul au plus juste, pour ne pas
 * avoir à revenir ici si le chemin d'interrogation s'allonge d'un objet. */
#define MOONRAKER_URL_OCTETS (BACKEND_HOTE_LONGUEUR_MAX + 128)

/* Délai appliqué à CHAQUE opération socket individuelle (connexion, lecture) :
 * assez long pour un aller-retour LAN chargé (le Raspberry Pi qui héberge
 * Moonraker peut lui-même être occupé à écrire du G-code sur la carte SD),
 * assez court pour qu'un délai isolé ne bloque pas des dizaines de secondes.
 *
 * Ce délai NE borne PAS la durée totale d'un cycle : esp_http_client_read()
 * rend un compte positif, pas une erreur, tant qu'il reste ne serait-ce qu'un
 * octet avant l'expiration de CE délai précis — un hôte qui égoutte sa
 * réponse un octet à la fois resterait donc sous ce seuil indéfiniment.
 * MOONRAKER_DELAI_TOTAL_MS ci-dessous couvre ce cas. */
#define MOONRAKER_DELAI_MS 3000

/* Plafond du cycle entier (connexion + en-têtes + lecture du corps), mesuré
 * au temps horloge via esp_timer_get_time() : deux fois le délai par
 * opération. Sans ce plafond, un hôte qui dégoutte sa réponse ferait tourner
 * rafraichir() indéfiniment (borné seulement par le remplissage des 4 Kio du
 * tampon statique, potentiellement des heures) SANS jamais appeler
 * liaison_echec() — l'écran afficherait alors des données vieilles de plusieurs
 * heures comme si elles venaient d'arriver, exactement ce que le grisage de
 * liaison.h existe pour empêcher. */
#define MOONRAKER_DELAI_TOTAL_MS (2 * MOONRAKER_DELAI_MS)

/* Hôte mémorisé par demarrer(), puisque rafraichir()/commande() ne le
 * reçoivent pas en paramètre (voir le contrat dans backend.h). Un seul
 * backend Moonraker tourne à la fois dans le socle — même hypothèse que
 * backend_factice.c pour son scénario courant — donc une variable statique
 * suffit. */
static backend_hote_t g_hote;
static bool           g_actif = false;

/* Client HTTP créé UNE SEULE FOIS par demarrer() et réutilisé à chaque appel
 * de rafraichir()/commande(), libéré par arreter(). L'appeler depuis
 * esp_http_client_init() à chaque cycle — comme le faisait la première
 * version de ce fichier — coûte environ dix-sept paires alloc/free par appel
 * (structures client/requête/réponse, tampons d'émission et de réception,
 * settings et liste de parseur, et les strdup() du schéma, de l'hôte, du
 * chemin, de l'URL et de chaque en-tête — voir esp_http_client_init() dans
 * esp_http_client.c) et ouvre une nouvelle connexion TCP à chaque fois.
 * Une fois par seconde pendant des heures, c'est exactement l'allocation
 * répétée que la règle « pas d'allocation dans le chemin de rafraîchissement »
 * interdit, en plus d'épuiser au fil de la journée le pool de connexions en
 * TIME_WAIT du système (une connexion neuve par cycle, ~86 400 par jour,
 * contre un pool par défaut de 16 PCB). Ici, seule esp_http_client_set_url()
 * / esp_http_client_set_method() changent à chaque requête — schéma emprunté
 * tel quel à l'exemple officiel http_native_request() de esp_http_client. */
static esp_http_client_handle_t g_client = NULL;

/* Construit "http://<adresse>:<port>/<chemin>" dans `tampon`. `chemin` ne
 * doit pas commencer par '/'. */
static void moonraker_construire_url(char *tampon, size_t taille, const char *chemin)
{
    snprintf(tampon, taille, "http://%s:%u/%s", g_hote.adresse, (unsigned)g_hote.port, chemin);
}

/* Ferme la connexion ET libère le cache éventuellement rempli par
 * esp_http_client_fetch_headers() — à appeler sur CHAQUE sortie de
 * moonraker_requete() qui a atteint esp_http_client_open(), pas seulement
 * en cas d'erreur.
 *
 * Quand des octets de corps arrivent groupés avec les en-têtes dans le même
 * segment TCP, esp_http_client_fetch_headers() les met de côté dans un
 * tampon interne alloué par realloc() (cache_data_in_fetch_hdr, un champ
 * privé de esp_http_client_t, jamais désactivable depuis l'API publique hors
 * esp_http_client_perform() — voir le commentaire sur g_client). Avant que
 * g_client ne soit réutilisé d'un cycle à l'autre (revue tâche 8, tour 1),
 * ce cache disparaissait avec esp_http_client_cleanup() à chaque appel :
 * personne n'avait besoin de le vider explicitement. Ce n'est plus vrai :
 * ni esp_http_client_close(), ni esp_http_client_prepare() (appelé par
 * open() au cycle suivant), ni esp_http_client_set_url() (qui ne touche au
 * cache que si l'hôte ou le port CHANGENT — jamais le cas ici, un seul hôte
 * pour toute la durée de vie du backend) ne le libèrent. Sans cet appel
 * explicite, un cycle qui n'atteint jamais la boucle de lecture (délai total
 * déjà dépassé à la sortie de fetch_headers(), voir moonraker_requete())
 * laisse ce cache en place ; http_on_body() y AJOUTE au cycle suivant plutôt
 * que de l'écraser, sans jamais remettre raw_len à zéro — la "réponse"
 * grandit d'un corps par cycle tant que l'hôte reste lent, jusqu'à épuiser
 * le tas sur l'appareil qu'on ne peut pas rebrancher pendant des heures. */
static void moonraker_fermer(void)
{
    esp_http_client_close(g_client);
    esp_http_client_clear_response_buffer(g_client);
}

/* Émet une requête (GET ou POST sans corps) vers `chemin` sur le client
 * partagé `g_client`, lit la réponse dans le tampon statique et rend sa
 * longueur utile dans `*longueur_lue` (peut être NULL si l'appelant — le
 * chemin des commandes — n'a pas besoin du corps). Ne modifie jamais le
 * tampon `etat` de l'appelant : c'est à `moonraker_parse_status`, plus haut
 * dans `backend_moonraker_rafraichir()`, de décider ce qu'il advient de
 * l'état déjà en place.
 *
 * Le statut HTTP est vérifié EN PREMIER, avant toute question de troncature
 * ou de délai : un statut d'erreur (401, 503...) peut parfaitement
 * accompagner une réponse courte que les tests suivants qualifieraient sinon
 * à tort de « tronquée ». Sans câble série, /log est le seul canal de
 * diagnostic de cet appareil — le distinguo compte. */
static esp_err_t moonraker_requete(esp_http_client_method_t methode, const char *chemin,
                                    size_t *longueur_lue)
{
    char url[MOONRAKER_URL_OCTETS];
    moonraker_construire_url(url, sizeof(url), chemin);

    if (esp_http_client_set_url(g_client, url) != ESP_OK ||
        esp_http_client_set_method(g_client, methode) != ESP_OK) {
        JOURNAL_ERREUR(TAG, "configuration de la requete impossible pour %s", url);
        return ESP_FAIL;
    }

    int64_t debut_us = esp_timer_get_time();

    esp_err_t erreur = esp_http_client_open(g_client, 0);
    if (erreur != ESP_OK) {
        JOURNAL_ALERTE(TAG, "connexion a %s impossible (%s)", url, esp_err_to_name(erreur));
        moonraker_fermer(); /* sans effet si rien n'etait ouvert ; remet l'etat a plat */
        return erreur;
    }

    /* content_length peut valoir -1 (reponse en chunked) : on ne s'y fie pas
     * pour dimensionner quoi que ce soit, la lecture ci-dessous s'arrete
     * d'elle-meme a la fin des donnees ou au bord du tampon statique. */
    (void)esp_http_client_fetch_headers(g_client);

    /* Verifie le plafond total des maintenant, meme si la boucle de lecture
     * ci-dessous ne s'execute jamais : esp_http_client_fetch_headers() peut a
     * elle seule avoir consomme tout le budget sur un hote qui degoutte ses
     * en-tetes, et rien ne nous a permis de l'interrompre pendant qu'elle
     * tournait (c'est un appel bloquant unique, pas une boucle qu'on
     * controle). */
    bool depasse = ((esp_timer_get_time() - debut_us) / 1000) >= MOONRAKER_DELAI_TOTAL_MS;

    size_t total = 0;
    while (!depasse && total < sizeof(g_tampon_reponse) - 1) {
        int lu = esp_http_client_read(g_client, g_tampon_reponse + total,
                                       sizeof(g_tampon_reponse) - 1 - total);
        if (lu <= 0) {
            break;
        }
        total += (size_t)lu;
        depasse = ((esp_timer_get_time() - debut_us) / 1000) >= MOONRAKER_DELAI_TOTAL_MS;
    }
    g_tampon_reponse[total] = '\0';

    bool complete = esp_http_client_is_complete_data_received(g_client);
    int statut = esp_http_client_get_status_code(g_client);
    moonraker_fermer();

    if (statut <= 0) {
        /* esp_http_client_fetch_headers() initialise status_code a -1 et ne
         * le change que si des entetes ont effectivement ete recues et
         * analysees : un statut <= 0 ici ne decrit donc PAS une reponse HTTP
         * (ce n'est le code d'aucun serveur reel), seulement l'absence totale
         * d'entetes exploitables. Le distinguer du "statut HTTP %d" ci-dessous
         * evite d'afficher un trompeur "statut HTTP -1" qui masquerait la
         * vraie cause (delai depasse ou connexion coupee avant meme les
         * entetes) sur le seul canal de diagnostic de cet appareil. */
        if (depasse) {
            JOURNAL_ALERTE(TAG, "%s : delai total de %d ms depasse avant reception des entetes",
                           url, MOONRAKER_DELAI_TOTAL_MS);
            return ESP_ERR_TIMEOUT;
        }
        JOURNAL_ALERTE(TAG, "%s : connexion interrompue avant reception des entetes", url);
        return ESP_FAIL;
    }
    if (statut < 200 || statut >= 300) {
        JOURNAL_ALERTE(TAG, "statut HTTP %d sur %s", statut, url);
        return ESP_FAIL;
    }
    if (depasse) {
        JOURNAL_ALERTE(TAG, "%s : delai total de %d ms depasse (recu %u octets)",
                       url, MOONRAKER_DELAI_TOTAL_MS, (unsigned)total);
        return ESP_ERR_TIMEOUT;
    }
    if (!complete) {
        if (total >= sizeof(g_tampon_reponse) - 1) {
            /* Le bord du tampon statique a ete atteint AVANT la fin des
             * donnees : c'est une vraie troncature, jamais grandie (voir le
             * commentaire sur MOONRAKER_TAMPON_OCTETS). */
            JOURNAL_ALERTE(TAG, "reponse de %s tronquee au-dela de %u octets ; ignoree",
                           url, (unsigned)sizeof(g_tampon_reponse) - 1u);
            return ESP_ERR_INVALID_SIZE;
        }
        /* Incomplete SANS avoir rempli le tampon : la lecture s'est arretee
         * pour une autre raison (connexion coupee, reinitialisee par le
         * serveur...). Ne pas la confondre avec une troncature : le tampon
         * avait de la place, ce n'est pas lui la cause. */
        JOURNAL_ALERTE(TAG, "%s : connexion interrompue apres %u octets (sur %u attendus)",
                       url, (unsigned)total, (unsigned)sizeof(g_tampon_reponse) - 1u);
        return ESP_FAIL;
    }

    if (longueur_lue != NULL) {
        *longueur_lue = total;
    }
    return ESP_OK;
}

static esp_err_t backend_moonraker_demarrer(void *etat, const backend_hote_t *hote)
{
    if (hote == NULL) {
        JOURNAL_ERREUR(TAG, "demarrage sans hote");
        return ESP_ERR_INVALID_ARG;
    }

    /* Remise a zero defensive, comme backend_factice_demarrer() : ce backend
     * ne suppose rien de l'etat qu'on lui tend. */
    memset(etat, 0, sizeof(etat_klipper_t));

    /* Au cas ou demarrer() serait rappele sans arreter() intermediaire (la
     * boucle actuelle ne le fait jamais, mais rien dans ce fichier ne doit
     * en dependre) : ne pas fuir un client deja cree. */
    if (g_client != NULL) {
        esp_http_client_cleanup(g_client);
        g_client = NULL;
    }

    g_hote = *hote;

    /* URL initiale construite avec le VRAI hôte, pas un espace réservé :
     * esp_http_client_init() calcule l'en-tête "Host: <hote>:<port>" une
     * fois pour toutes à partir de cette URL (_get_host_header() dans
     * esp_http_client.c) ; esp_http_client_set_url(), appelée à chaque
     * requête dans moonraker_requete(), ne recalcule cet en-tête que si
     * l'hôte OU le port changent d'un appel à l'autre — ce qui n'arrive
     * jamais ici, un seul hôte pour toute la durée de vie du backend. Un
     * espace réservé du genre "http://127.0.0.1/" laisserait donc le port
     * hors de l'en-tête Host pour toutes les requêtes suivantes (Moonraker
     * ne s'en soucie pas, mais un éventuel relais inverse devant lui le
     * pourrait). Le chemin importe peu : chaque requête le remplace de toute
     * façon via set_url(). */
    char url_initiale[MOONRAKER_URL_OCTETS];
    moonraker_construire_url(url_initiale, sizeof(url_initiale), "");
    esp_http_client_config_t config = {
        .url = url_initiale,
        .timeout_ms = MOONRAKER_DELAI_MS,
    };
    g_client = esp_http_client_init(&config);
    if (g_client == NULL) {
        JOURNAL_ERREUR(TAG, "esp_http_client_init a echoue");
        return ESP_FAIL;
    }

    g_actif = true;

    JOURNAL_INFO(TAG, "demarrage (hote=%s port=%u)", hote->adresse, (unsigned)hote->port);
    return ESP_OK;
}

static esp_err_t backend_moonraker_rafraichir(void *etat)
{
    if (!g_actif) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t longueur = 0;
    esp_err_t erreur = moonraker_requete(HTTP_METHOD_GET, MOONRAKER_CHEMIN_INTERROGATION, &longueur);
    if (erreur != ESP_OK) {
        return erreur;
    }

    if (!moonraker_parse_status(g_tampon_reponse, longueur, (etat_klipper_t *)etat)) {
        JOURNAL_ALERTE(TAG, "reponse Moonraker inexploitable (result.status absent ou JSON malforme)");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void backend_moonraker_arreter(void *etat)
{
    (void)etat;
    g_actif = false;
    if (g_client != NULL) {
        esp_http_client_cleanup(g_client);
        g_client = NULL;
    }
    JOURNAL_INFO(TAG, "arret");
}

static esp_err_t backend_moonraker_commande(void *etat, const char *action,
                                             const char *arguments_json)
{
    (void)etat;
    (void)arguments_json; /* aucune des quatre actions ci-dessous ne prend de corps */

    if (!g_actif) {
        /* Meme garde que backend_moonraker_rafraichir() : g_client n'existe
         * que si demarrer() a reussi. Aucun chemin actuel de core/boucle.c ne
         * peut appeler commande() avant cela, mais le verifier ici coute peu
         * et evite qu'un futur changement d'ordonnancement ne deverrouille un
         * appel a esp_http_client_set_url(NULL, ...). */
        return ESP_ERR_INVALID_STATE;
    }

    const char *chemin;
    if (strcmp(action, BACKEND_ACTION_PAUSE) == 0) {
        chemin = "printer/print/pause";
    } else if (strcmp(action, BACKEND_ACTION_REPRENDRE) == 0) {
        chemin = "printer/print/resume";
    } else if (strcmp(action, BACKEND_ACTION_ANNULER) == 0) {
        chemin = "printer/print/cancel";
    } else if (strcmp(action, BACKEND_ACTION_URGENCE) == 0) {
        chemin = "printer/emergency_stop";
    } else {
        /* Une action inconnue doit echouer fort et explicitement, pour que
         * l'interface puisse griser un bouton en connaissant la raison —
         * jamais l'ignorer en silence (meme regle que backend_factice.c). */
        JOURNAL_ALERTE(TAG, "commande inconnue %s", action);
        return ESP_ERR_NOT_SUPPORTED;
    }

    JOURNAL_INFO(TAG, "commande %s -> POST /%s", action, chemin);
    return moonraker_requete(HTTP_METHOD_POST, chemin, NULL);
}

static const backend_desc_t g_backend_moonraker_desc = {
    .nom = "moonraker",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = backend_moonraker_demarrer,
    .rafraichir = backend_moonraker_rafraichir,
    .arreter = backend_moonraker_arreter,
    .commande = backend_moonraker_commande,
};

const backend_desc_t *backend_moonraker_desc(void)
{
    return &g_backend_moonraker_desc;
}
