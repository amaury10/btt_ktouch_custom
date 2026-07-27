#include "backend_moonraker.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_timer.h"

#include "etat_klipper.h"
#include "journal.h"
#include "liaison.h"
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
 * Ce tampon reste le SEUL stockage variable DE CE FICHIER qui touche le tas :
 * voir g_client ci-dessous pour ce qui a changé depuis la première version
 * (revue de la tâche 8, tour 1) — le client HTTP lui-même n'est plus recréé
 * à chaque cycle. Précision qui compte (revue de fin de jalon 2a) : ce n'est
 * vrai QUE de ce fichier. moonraker_parse_status() (moonraker_parse.c),
 * appelée juste après par backend_moonraker_rafraichir() sur chaque cycle
 * réussi, construit un arbre cJSON complet via cJSON_ParseWithLength() puis
 * le détruit — mesuré à 84 allocations/libérations par cycle, ~2,4 millions
 * sur huit heures. Équilibré (84 allocs pour 84 free, rien ne fuit) mais
 * c'est une churn de tas bien réelle, pas rien : sur un appareil qui tourne
 * des jours sans redémarrer, c'est exactement le genre de motif répété qui
 * fragmente un allocateur, même sans fuite. Voir le log de minimum de tas
 * libre dans core/boucle.c (boucle_tache()) pour transformer cette hypothèse
 * en mesure sur le premier essai matériel. */
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
 * esp_http_client.c). Une fois par seconde pendant des heures, c'est
 * exactement l'allocation répétée que la règle « pas d'allocation dans le
 * chemin de rafraîchissement » interdit ; réutiliser l'OBJET client élimine
 * cette churn-là. Ici, seule esp_http_client_set_url()/
 * esp_http_client_set_method() changent à chaque requête — schéma emprunté
 * tel quel à l'exemple officiel http_native_request() de esp_http_client.
 *
 * Ce que ceci NE règle PAS (précision de la revue de fin de jalon 2a, la
 * version précédente de ce commentaire le laissait entendre à tort) : la
 * CONNEXION TCP, elle, reste ouverte puis fermée à CHAQUE requête —
 * moonraker_fermer() appelle esp_http_client_close() en sortie de
 * moonraker_requete(), objet client reutilisé ou non. C'est donc toujours une
 * connexion TCP neuve par cycle, ~86 400 par jour, avec la pression en
 * TIME_WAIT que cela entraîne côté client (un pool par défaut de 16 PCB côté
 * lwIP). Cette pression est acceptée ici, pas éliminée : lwIP recycle les
 * PCB en TIME_WAIT au besoin, et à un cycle par seconde le taux de
 * renouvellement reste très en-dessous de ce qui saturerait ce pool. La
 * distinguer clairement de la churn d'allocation (réglée, elle) évite de
 * chercher plus tard une fuite de connexions qui n'existe pas. */
static esp_http_client_handle_t g_client = NULL;

/* Construit "http://<adresse>:<port>/<chemin>" dans `tampon`. `chemin` ne
 * doit pas commencer par '/'. */
static void moonraker_construire_url(char *tampon, size_t taille, const char *chemin)
{
    snprintf(tampon, taille, "http://%s:%u/%s", g_hote.adresse, (unsigned)g_hote.port, chemin);
}

/* Cadencement du journal d'échecs de moonraker_requete() (revue de fin de
 * jalon 2a, IMPORTANT 4) : sans lui, un hôte Moonraker injoignable produit un
 * JOURNAL_ALERTE identique à CHAQUE cycle (une fois par seconde), chacun
 * portant l'URL complète — environ 197 octets avec le préfixe de log. Le
 * tampon netlog fait 16 Kio, soit ~83 lignes : une panne Moonraker de 83
 * secondes remplacerait donc l'intégralité de la séquence de démarrage
 * (partition, sauvetage armé, source des identifiants WiFi, réglages
 * chargés) — le seul canal de diagnostic d'un appareil sans port série, et
 * cela arrive précisément quand quelque chose ne va pas. Même défaut de
 * conception que la marge de pile bornée dans bb16a08, un fichier plus loin.
 *
 * g_liaison_journal est un suivi PRIVÉ à ce fichier, indépendant du
 * liaison_t réellement publié par /state (propriété de core/boucle.c) : il
 * ne sert qu'à décider QUAND journaliser, jamais ce qui est publié. Mêmes
 * seuils par défaut (liaison.h) pour garder la même sensibilité aux échecs
 * consécutifs. */
static liaison_t g_liaison_journal;
static bool      g_liaison_journal_prete;
static int64_t   g_dernier_journal_echec_us;

#define MOONRAKER_JOURNAL_INTERVALLE_US (60LL * 1000 * 1000)

/* À appeler pour CHAQUE échec de moonraker_requete(), avant de décider s'il
 * faut le journaliser. Rend true si CET échec doit produire une ligne de
 * journal — le tout premier d'une série, une transition d'état de
 * g_liaison_journal (EN_LIGNE -> DEGRADEE -> HORS_LIGNE), ou l'écoulement de
 * MOONRAKER_JOURNAL_INTERVALLE_US depuis la dernière ligne — et alors met
 * `*avec_url` à true seulement pour le tout premier échec de la série :
 * l'hôte est déjà journalisé au démarrage (voir backend_moonraker_demarrer())
 * et au tout premier échec ci-dessous, le répéter à chaque ligne suivante
 * coûte un espace de journal précieux pour une information déjà disponible. */
static bool moonraker_journal_echec_pret(bool *avec_url)
{
    if (!g_liaison_journal_prete) {
        liaison_init(&g_liaison_journal, LIAISON_SEUIL_DEGRADE_DEFAUT, LIAISON_SEUIL_HORS_LIGNE_DEFAUT);
        g_liaison_journal_prete = true;
        g_dernier_journal_echec_us = 0;
    }

    liaison_etat_t avant = liaison_etat(&g_liaison_journal);
    liaison_echec(&g_liaison_journal);
    liaison_etat_t apres = liaison_etat(&g_liaison_journal);

    bool premier = (liaison_echecs_consecutifs(&g_liaison_journal) == 1);
    bool transition = (apres != avant);
    int64_t maintenant = esp_timer_get_time();
    bool intervalle_ecoule = (maintenant - g_dernier_journal_echec_us) >= MOONRAKER_JOURNAL_INTERVALLE_US;

    *avec_url = premier;

    if (premier || transition || intervalle_ecoule) {
        g_dernier_journal_echec_us = maintenant;
        return true;
    }
    return false;
}

/* À appeler sur CHAQUE succès de moonraker_requete() : efface la série
 * d'échecs en cours, pour que la prochaine panne recommence par un "premier
 * échec" (journalisé avec l'URL complète) plutôt que de rester silencieuse si
 * elle survient après une transition déjà consommée par une panne
 * précédente. */
static void moonraker_journal_echec_reinitialiser(void)
{
    if (g_liaison_journal_prete) {
        liaison_succes(&g_liaison_journal);
    }
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
        bool avec_url;
        if (moonraker_journal_echec_pret(&avec_url)) {
            if (avec_url) {
                JOURNAL_ALERTE(TAG, "connexion a %s impossible (%s)", url, esp_err_to_name(erreur));
            } else {
                JOURNAL_ALERTE(TAG, "connexion impossible (%s) (echecs consecutifs : %u)",
                               esp_err_to_name(erreur), (unsigned)liaison_echecs_consecutifs(&g_liaison_journal));
            }
        }
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
        bool avec_url;
        if (depasse) {
            if (moonraker_journal_echec_pret(&avec_url)) {
                if (avec_url) {
                    JOURNAL_ALERTE(TAG, "%s : delai total de %d ms depasse avant reception des entetes",
                                   url, MOONRAKER_DELAI_TOTAL_MS);
                } else {
                    JOURNAL_ALERTE(TAG, "delai total de %d ms depasse avant reception des entetes",
                                   MOONRAKER_DELAI_TOTAL_MS);
                }
            }
            return ESP_ERR_TIMEOUT;
        }
        if (moonraker_journal_echec_pret(&avec_url)) {
            if (avec_url) {
                JOURNAL_ALERTE(TAG, "%s : connexion interrompue avant reception des entetes", url);
            } else {
                JOURNAL_ALERTE(TAG, "connexion interrompue avant reception des entetes");
            }
        }
        return ESP_FAIL;
    }
    if (statut < 200 || statut >= 300) {
        bool avec_url;
        if (moonraker_journal_echec_pret(&avec_url)) {
            if (avec_url) {
                JOURNAL_ALERTE(TAG, "statut HTTP %d sur %s", statut, url);
            } else {
                JOURNAL_ALERTE(TAG, "statut HTTP %d", statut);
            }
        }
        return ESP_FAIL;
    }
    if (depasse) {
        bool avec_url;
        if (moonraker_journal_echec_pret(&avec_url)) {
            if (avec_url) {
                JOURNAL_ALERTE(TAG, "%s : delai total de %d ms depasse (recu %u octets)",
                               url, MOONRAKER_DELAI_TOTAL_MS, (unsigned)total);
            } else {
                JOURNAL_ALERTE(TAG, "delai total de %d ms depasse (recu %u octets)",
                               MOONRAKER_DELAI_TOTAL_MS, (unsigned)total);
            }
        }
        return ESP_ERR_TIMEOUT;
    }
    if (!complete) {
        bool avec_url;
        if (total >= sizeof(g_tampon_reponse) - 1) {
            /* Le bord du tampon statique a ete atteint AVANT la fin des
             * donnees : c'est une vraie troncature, jamais grandie (voir le
             * commentaire sur MOONRAKER_TAMPON_OCTETS). */
            if (moonraker_journal_echec_pret(&avec_url)) {
                if (avec_url) {
                    JOURNAL_ALERTE(TAG, "reponse de %s tronquee au-dela de %u octets ; ignoree",
                                   url, (unsigned)sizeof(g_tampon_reponse) - 1u);
                } else {
                    JOURNAL_ALERTE(TAG, "reponse tronquee au-dela de %u octets ; ignoree",
                                   (unsigned)sizeof(g_tampon_reponse) - 1u);
                }
            }
            return ESP_ERR_INVALID_SIZE;
        }
        /* Incomplete SANS avoir rempli le tampon : la lecture s'est arretee
         * pour une autre raison (connexion coupee, reinitialisee par le
         * serveur...). Ne pas la confondre avec une troncature : le tampon
         * avait de la place, ce n'est pas lui la cause. */
        if (moonraker_journal_echec_pret(&avec_url)) {
            if (avec_url) {
                JOURNAL_ALERTE(TAG, "%s : connexion interrompue apres %u octets (sur %u attendus)",
                               url, (unsigned)total, (unsigned)sizeof(g_tampon_reponse) - 1u);
            } else {
                JOURNAL_ALERTE(TAG, "connexion interrompue apres %u octets (sur %u attendus)",
                               (unsigned)total, (unsigned)sizeof(g_tampon_reponse) - 1u);
            }
        }
        return ESP_FAIL;
    }

    /* Succes : la serie d'echecs en cours (s'il y en avait une) se termine
     * ici, pour que la prochaine panne reparte d'un "premier echec"
     * journalise avec l'URL complete plutot que de rester silencieuse. */
    moonraker_journal_echec_reinitialiser();

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

    /* Un nouveau demarrage repart d'un journal d'echecs vierge : un hote
     * different (ou le meme, reconfigure) ne doit pas heriter du silence
     * accumule par une panne du demarrage precedent. */
    g_liaison_journal_prete = false;
    g_dernier_journal_echec_us = 0;

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
