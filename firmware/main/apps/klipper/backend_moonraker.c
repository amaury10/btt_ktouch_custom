#include "backend_moonraker.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_timer.h"

#include "backend.h"
#include "etat_klipper.h"
#include "journal.h"
#include "klipper_gcode.h"
#include "liaison.h"
#include "moonraker_parse.h"
#include "moonraker_rpc.h"
#include "moonraker_ws.h"

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
 * le détruit — de l'ordre de 80 allocations/libérations par cycle (estimation
 * de la revue, jamais mesurée sur l'appareil), soit quelques millions sur
 * huit heures. Équilibré (autant de free que d'allocs, rien ne fuit) mais
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
 * avoir à revenir ici si le chemin d'interrogation s'allonge d'un objet --
 * cette marge couvre aussi, sans qu'il soit besoin de la grossir, les deux
 * octets de crochets qu'une adresse IPv6 ajoute (voir
 * moonraker_construire_url()). */
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

/* Jalon 3a, tache 5 : derniere image WS complete connue, portee ICI en
 * statique -- exactement comme g_progression_scenario1 dans
 * backend_factice.c (voir son commentaire) : `etat` recu par rafraichir()
 * pointe TOUJOURS vers un tampon fraichement remis a zero par le socle
 * (contrat backend.h), donc y relire quoi que ce soit rendrait toujours 0.
 * Sert a publier un cycle de succes meme quand la boite n'a RIEN de neuf a
 * drainer (le WS pousse par evenement, pas a chaque cycle de la boucle a
 * 250 ms) -- sans cette retention, la plupart des cycles echoueraient faute
 * de nouveaute et griseraient l'ecran a tort (voir backend_moonraker_rafraichir()
 * plus bas). `g_dernier_etat_ws_valide` reste false tant qu'aucun premier
 * depot n'a jamais ete draine depuis le demarrage (ou une reconnexion) --
 * dans cette fenetre tres courte, il n'y a rien d'honnete a publier. */
static etat_klipper_t g_dernier_etat_ws;
static bool           g_dernier_etat_ws_valide = false;

/* Revue tache 5, fix round 1 (L4) : dernier compteur de reconnexions WS VU
 * par rafraichir() -- sert a detecter qu'une reconnexion a eu lieu DEPUIS le
 * cycle precedent, meme si `moonraker_ws_en_ligne()` est deja revenu a true
 * au moment ou ce cycle tourne (le compteur, lui, avance AVANT que
 * WEBSOCKET_EVENT_CONNECTED ne remette g_connecte a true -- voir
 * minuterie_reconnexion_cb() dans moonraker_ws.c -- donc il est deja a jour
 * des le premier cycle qui suit une reconnexion). Sans ce controle,
 * g_dernier_etat_ws (potentiellement vieux de plusieurs secondes, datant
 * d'AVANT la coupure) survivrait tel quel a une reconnexion et serait
 * republie comme si de rien n'etait des le premier cycle ou WS redevient en
 * ligne -- violation de spec §7 ("jamais l'etat de l'ancienne connexion
 * presente comme celui de la nouvelle", ecrit pour le parc de machines de
 * 3f mais valable ici a l'identique pour une simple coupure/reprise). */
static uint32_t g_dernier_compteur_reconnexions_ws = 0;

/* Delai borne d'une commande RPC corrolee sur le WS (moonraker_ws_commande()) :
 * du meme ordre que MOONRAKER_DELAI_TOTAL_MS ci-dessous pour le chemin HTTP --
 * assez long pour un aller-retour Moonraker charge, assez court pour qu'un
 * bouton reste explicable (Command failed) plutot que fige indefiniment.
 * Sondee par petites tranches par moonraker_ws_commande() elle-meme (voir
 * son commentaire de tete dans moonraker_ws.c) : cette valeur ne bloque que
 * boucle_traiter_commandes() (core/boucle.c), jamais la tache WS. */
#define MOONRAKER_WS_COMMANDE_TIMEOUT_MS 5000u

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
 * doit pas commencer par '/'.
 *
 * `g_hote.adresse` est stockée SANS crochets, même pour un littéral IPv6
 * (hote_parse.c les retire à l'analyse -- voir son commentaire de tête,
 * revue de la tâche 8, round 1) : les remettre est donc le travail de CE
 * site, le seul qui assemble une URL à partir de `g_hote`. Sans eux,
 * "fe80::1" donnerait "http://fe80::1:7125/..." -- une URL elle-même
 * ambiguë (impossible de distinguer les ':' de l'adresse de celui du port),
 * non conforme à RFC 3986 §3.2.2 qui exige la forme "[adresse]" pour tout
 * hôte IPv6 littéral dans une URI. Détecté ici par la seule présence d'un
 * ':' dans l'adresse stockée : hote_parse() garantit déjà qu'une adresse
 * acceptée qui contient un ':' provient forcément de la forme entre
 * crochets (voir hote_parse_sans_crochets(), qui rejette toute adresse
 * candidate contenant elle-même un ':'), jamais d'un hasard. */
static void moonraker_construire_url(char *tampon, size_t taille, const char *chemin)
{
    if (strchr(g_hote.adresse, ':') != NULL) {
        snprintf(tampon, taille, "http://[%s]:%u/%s", g_hote.adresse, (unsigned)g_hote.port, chemin);
    } else {
        snprintf(tampon, taille, "http://%s:%u/%s", g_hote.adresse, (unsigned)g_hote.port, chemin);
    }
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

    /* Jalon 3a, tache 5 : plus aucune image WS valide tant que le nouveau
     * client (demarre juste en dessous) n'a pas fourni son premier depot --
     * memes valeurs qu'a froid, voir le commentaire de declaration. */
    memset(&g_dernier_etat_ws, 0, sizeof(g_dernier_etat_ws));
    g_dernier_etat_ws_valide = false;
    g_dernier_compteur_reconnexions_ws = 0;

    /* Le WS demarre EN PLUS du client HTTP ci-dessus, jamais a sa place : le
     * HTTP reste le repli tant que le WS n'est pas en ligne (spec §4). Un
     * echec de demarrage du WS n'est PAS fatal a ce backend -- il degrade
     * simplement en HTTP pur, exactement comme un WS qui se deconnecterait
     * plus tard.
     *
     * Fix round 1 (revue tache 5, L3) : CE journal ne doit PAS promettre une
     * nouvelle tentative qui n'existe pas -- moonraker_ws_demarrer() echoue
     * seulement sur un defaut d'ALLOCATION locale (mutex, minuterie
     * esp_timer, esp_websocket_client_init()), jamais sur une simple
     * indisponibilite reseau (qui, elle, est geree APRES un demarrage
     * reussi par le backoff de reconnexion de moonraker_ws.c -- voir
     * minuterie_reconnexion_cb()) : aucun mecanisme de ce fichier ne
     * rappelle moonraker_ws_demarrer() plus tard si CET appel-la echoue. Le
     * repli HTTP, lui, reste bien reel et permanent (moonraker_ws_en_ligne()
     * restera false pour toute la duree de vie de ce backend) -- c'est la
     * seule promesse honnete que ce message peut tenir tant qu'aucune
     * retentative differee n'est implementee. */
    esp_err_t erreur_ws = moonraker_ws_demarrer(hote);
    if (erreur_ws != ESP_OK) {
        JOURNAL_ALERTE(TAG,
            "demarrage du client WS impossible (%s) ; repli HTTP permanent pour ce backend (aucune "
            "nouvelle tentative programmee)",
            esp_err_to_name(erreur_ws));
    }

    JOURNAL_INFO(TAG, "demarrage (hote=%s port=%u)", hote->adresse, (unsigned)hote->port);
    return ESP_OK;
}

static esp_err_t backend_moonraker_rafraichir(void *etat)
{
    if (!g_actif) {
        return ESP_ERR_INVALID_STATE;
    }

    if (moonraker_ws_en_ligne()) {
        /* Fix round 1 (revue tache 5, L1+L4) : une reconnexion depuis le
         * dernier cycle invalide l'image retenue AVANT toute autre chose --
         * voir le commentaire de g_dernier_compteur_reconnexions_ws. Place
         * ICI, avant le drain qui suit : si un depot frais (le nouvel
         * instantane de l'abonnement, renvoye a CHAQUE reconnexion) est deja
         * disponible ce meme cycle, il revalide immediatement ; sinon ce
         * cycle echoue honnetement (voir le bloc !g_dernier_etat_ws_valide
         * plus bas) plutot que de republier une image datant d'avant la
         * coupure. */
        uint32_t compteur_reco = moonraker_ws_compteur_reconnexions();
        if (compteur_reco != g_dernier_compteur_reconnexions_ws) {
            g_dernier_compteur_reconnexions_ws = compteur_reco;
            g_dernier_etat_ws_valide = false;
        }

        /* Draine la boite ; rien de neuf ⇒ garde l'image retenue (voir le
         * commentaire de g_dernier_etat_ws) plutot que d'echouer le cycle --
         * un cycle sans nouveaute n'est PAS un cycle en panne, le WS pousse
         * par evenement (Moonraker peut tres bien n'avoir rien a dire
         * pendant plusieurs cycles a 250 ms d'affilee, ex. machine au
         * repos). Echouer ici a chaque fois griserait l'ecran a tort. */
        etat_klipper_t nouveau;
        if (moonraker_ws_drainer(&nouveau)) {
            g_dernier_etat_ws = nouveau;
            g_dernier_etat_ws_valide = true;
        }

        if (!g_dernier_etat_ws_valide) {
            /* Fenetre tres courte juste apres demarrer()/une reconnexion :
             * aucune image n'a encore ete deposee (meme pas l'instantane
             * initial de l'abonnement). Rien d'honnete a publier -- le
             * cycle echoue, l'ecran reste sur son dernier etat connu
             * (grise par la liaison), jamais un tampon vide presente comme
             * reel. */
            return ESP_FAIL;
        }

        memcpy((etat_klipper_t *)etat, &g_dernier_etat_ws, sizeof(etat_klipper_t));

        /* La liaison : WS connecte ET Klippy pret ⇒ succes de cycle ; WS
         * connecte mais Klippy down ⇒ echec -- MEME distinction que le
         * chemin HTTP ci-dessous (un statut HTTP inexploitable ou un
         * webhooks.state "shutdown" y font deja echouer le cycle, voir
         * moonraker_parse_status()). Jamais de repli HTTP dans ce cas
         * precis : l'echec doit rester visible, pas masque par un second
         * transport qui interrogerait la meme machine en panne. */
        return moonraker_ws_klippy_pret() ? ESP_OK : ESP_FAIL;
    }

    /* WS hors ligne : repli HTTP existant, INCHANGE depuis le jalon 2a. */
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
    moonraker_ws_arreter();
    if (g_client != NULL) {
        esp_http_client_cleanup(g_client);
        g_client = NULL;
    }
    JOURNAL_INFO(TAG, "arret");
}

/* Tache 6 : extrait `nom` de `arguments_json = {"nom":"<macro>"}` --
 * BACKEND_ACTION_MACRO en garantit la presence (voir core/backend.h), au
 * contraire des quatre actions communes qui n'en prennent jamais. cJSON ici
 * (pas le strstr borne de backend_factice.c) : CE backend-ci EST le vrai
 * consommateur d'un JSON dont la forme lui vient en dernier ressort de
 * l'ecran (ecran_macros.c, deja construit avec
 * ecran_macros_construire_arguments()) -- mais rien ne garantit qu'un futur
 * appelant respecte cette forme a la lettre, et cJSON tolere les variations
 * (espaces, ordre des cles) qu'un strstr borne ne tolererait pas. Rend false
 * SANS TOUCHER `sortie` si `arguments_json` est NULL, illisible, si "nom"
 * est absent/pas une chaine, ou si le nom ne tient pas dans `taille`. */
static bool moonraker_extraire_nom_macro(const char *arguments_json, char *sortie, size_t taille)
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

/* Taille de tampon suffisante pour le nom d'une macro (voir
 * KLIPPER_MACRO_NOM_MAX, etat_klipper.h) -- ce backend n'a pas de contrat
 * direct avec l'etat ici (arguments_json vient de l'ecran, pas de l'etat),
 * mais c'est la meme borne que partout ailleurs dans ce seam
 * (backend_factice.c, moonraker_rpc.c). */
#define MOONRAKER_MACRO_NOM_MAX KLIPPER_MACRO_NOM_MAX

/* Tache 1, jalon 3b : extrait `script` de arguments_json = {"script":"<gcode>"}
 * -- meme structure que moonraker_extraire_nom_macro() ci-dessus, calquee
 * comme demande par le brief de la tache : BACKEND_ACTION_GCODE en garantit
 * la presence (voir core/backend.h), cJSON pour la meme raison (l'appelant
 * "officiel" est klipper_gcode.c via un ecran, mais rien ne garantit qu'un
 * futur appelant respecte la forme a la lettre). Rend false SANS TOUCHER
 * `sortie` si `arguments_json` est NULL, illisible, si "script" est
 * absent/pas une chaine, ou si le script ne tient pas dans `taille`. */
static bool moonraker_extraire_champ_script(const char *arguments_json, char *sortie, size_t taille)
{
    if (arguments_json == NULL || sortie == NULL || taille == 0) {
        return false;
    }
    cJSON *racine = cJSON_Parse(arguments_json);
    if (racine == NULL) {
        return false;
    }
    const cJSON *script = cJSON_GetObjectItemCaseSensitive(racine, "script");
    bool ok = cJSON_IsString(script) && script->valuestring != NULL;
    if (ok) {
        int ecrit = snprintf(sortie, taille, "%s", script->valuestring);
        ok = (ecrit >= 0) && ((size_t)ecrit < taille);
    }
    cJSON_Delete(racine);
    return ok;
}

/* Codage pourcentage minimal (RFC 3986) pour embarquer un script gcode dans
 * la query string du repli HTTP (voir plus bas, "?script=<script>") :
 * contrairement a un nom de macro (alphanumerique + underscore uniquement,
 * jamais code cote HTTP dans ce seam), un script produit par klipper_gcode.c
 * contient couramment des espaces ("G1 X10 F3000") et, pour un jog
 * (klipper_gcode_jog()), de VRAIS retours a la ligne (SAVE_GCODE_STATE ...
 * \n G91 \n ...). Ni l'un ni l'autre n'est valide tel quel dans une query
 * string -- un espace non code casse le decoupage des parametres, et un
 * octet 0x0A brut a l'interieur d'une ligne de requete HTTP est une
 * violation de protocole (au mieux rejetee, au pire un risque d'injection
 * de requete). Seuls les caracteres surs tels quels dans une query string
 * (alphanumerique + "-_.~") restent inchanges ; tout le reste devient
 * "%XX". Rend false si `sortie` est trop court pour le resultat code
 * (jamais de troncature silencieuse, meme regle que le reste de ce
 * fichier). */
static bool moonraker_url_encoder_script(const char *script, char *sortie, size_t taille)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t pos = 0;
    for (const char *p = script; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        bool direct = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (direct) {
            if (pos + 1 >= taille) {
                return false;
            }
            sortie[pos++] = (char)c;
        } else {
            if (pos + 3 >= taille) {
                return false;
            }
            sortie[pos++] = '%';
            sortie[pos++] = HEX[(c >> 4) & 0x0Fu];
            sortie[pos++] = HEX[c & 0x0Fu];
        }
    }
    if (pos >= taille) {
        return false;
    }
    sortie[pos] = '\0';
    return true;
}

static esp_err_t backend_moonraker_commande(void *etat, const char *action,
                                             const char *arguments_json)
{
    (void)etat;

    if (!g_actif) {
        /* Meme garde que backend_moonraker_rafraichir() : g_client n'existe
         * que si demarrer() a reussi. Aucun chemin actuel de core/boucle.c ne
         * peut appeler commande() avant cela, mais le verifier ici coute peu
         * et evite qu'un futur changement d'ordonnancement ne deverrouille un
         * appel a esp_http_client_set_url(NULL, ...). */
        return ESP_ERR_INVALID_STATE;
    }

    bool est_macro = (strcmp(action, BACKEND_ACTION_MACRO) == 0);
    char nom_macro[MOONRAKER_MACRO_NOM_MAX];
    if (est_macro && !moonraker_extraire_nom_macro(arguments_json, nom_macro, sizeof(nom_macro))) {
        JOURNAL_ALERTE(TAG, "commande macro sans nom exploitable dans arguments_json");
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Tache 1, jalon 3b : meme structure que est_macro/nom_macro ci-dessus. */
    bool est_gcode = (strcmp(action, BACKEND_ACTION_GCODE) == 0);
    char script[KLIPPER_GCODE_MAX];
    if (est_gcode && !moonraker_extraire_champ_script(arguments_json, script, sizeof(script))) {
        JOURNAL_ALERTE(TAG, "commande gcode sans script exploitable dans arguments_json");
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Feature "Power devices Moonraker", tache B : ECART DELIBERE par
     * rapport a est_macro/est_gcode ci-dessus -- ni extraction ni
     * reconstruction du JSON ici, `arguments_json` EST deja les params
     * attendus par machine.device_power.post_device
     * ({"device":"...","action":"toggle"}, construits par l'ecran,
     * ecran_power.c) et lui est relaye tel quel (voir BACKEND_ACTION_POWER,
     * core/backend.h). Seule verification : un appelant qui invoque cette
     * action doit fournir des arguments, sinon rien a transmettre. */
    bool est_power = (strcmp(action, BACKEND_ACTION_POWER) == 0);
    if (est_power && arguments_json == NULL) {
        JOURNAL_ALERTE(TAG, "commande power sans arguments_json");
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Feature Spoolman : meme contrat que power juste au-dessus --
     * `arguments_json` EST deja les params de server.spoolman.post_spool_id
     * ({"spool_id":N} ou {"spool_id":null}), construits par l'ecran. */
    bool est_spoolman = (strcmp(action, BACKEND_ACTION_SPOOLMAN) == 0);
    if (est_spoolman && arguments_json == NULL) {
        JOURNAL_ALERTE(TAG, "commande spoolman sans arguments_json");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (moonraker_ws_en_ligne()) {
        /* Jalon 3a, tache 6 : BACKEND_ACTION_MACRO -> printer.gcode.script
         * {"script":"<nom>"} -- rpc_methode_commande() (tache 5) refuse
         * volontairement cette action (voir son commentaire dans
         * moonraker_rpc.h, "support WS laisse a la tache 6") car elle seule,
         * parmi les actions communes, prend des parametres : elle est donc
         * traitee a part ici, AVANT la table des quatre actions sans
         * parametre ci-dessous. */
        if (est_macro) {
            char params[MOONRAKER_MACRO_NOM_MAX + 16];
            int ecrit = snprintf(params, sizeof(params), "{\"script\":\"%s\"}", nom_macro);
            if (ecrit < 0 || (size_t)ecrit >= sizeof(params)) {
                JOURNAL_ALERTE(TAG, "construction des parametres macro impossible pour %s", nom_macro);
                return ESP_FAIL;
            }
            bool succes = false;
            char erreur[128];
            erreur[0] = '\0';
            esp_err_t erreur_ws = moonraker_ws_commande("printer.gcode.script", params,
                                                          MOONRAKER_WS_COMMANDE_TIMEOUT_MS, &succes, erreur,
                                                          sizeof(erreur));
            if (erreur_ws != ESP_OK) {
                JOURNAL_ALERTE(TAG, "commande macro %s (WS) en echec (%s)", nom_macro,
                               esp_err_to_name(erreur_ws));
                return erreur_ws;
            }
            /* LIMITE PROTOCOLAIRE (voir moonraker_rpc.h, pres de
             * rpc_lire_reponse/RPC_MSG_AUTRE, verifiee tache 4 contre un
             * vrai Klipper) : `succes` est quasi TOUJOURS true ici, y
             * compris pour une macro totalement inconnue -- Klipper repond
             * {"result":"ok"} dans les deux cas pour printer.gcode.script,
             * l'echec REEL n'arrivant que via notify_gcode_response
             * (RPC_MSG_AUTRE, non interprete par ce seam aujourd'hui, voir
             * le rapport de la tache 6). Ce test reste neanmoins le bon --
             * il capture les rejets JSON-RPC de PROTOCOLE (parametres
             * invalides, Klippy down), qui, eux, rendent bien
             * succes=false. */
            if (!succes) {
                JOURNAL_ALERTE(TAG, "macro %s refusee par Moonraker (%s)", nom_macro, erreur);
                return ESP_FAIL;
            }
            JOURNAL_INFO(TAG, "commande macro %s -> WS printer.gcode.script", nom_macro);
            return ESP_OK;
        }

        if (est_gcode) {
            /* Tache 1, jalon 3b : meme methode que la macro ci-dessus
             * (printer.gcode.script), mais le script arrive DEJA construit
             * (klipper_gcode.c) au lieu d'un nom a envelopper. ECART
             * DELIBERE par rapport au chemin macro pour la construction des
             * parametres : la macro ci-dessus batit son JSON a la main
             * (snprintf) parce qu'un nom Klipper valide est alphanumerique +
             * underscore, jamais d'espace ni de caractere special -- un
             * script gcode, lui, contient couramment des espaces
             * ("G1 X10 F3000") et, pour un jog (klipper_gcode_jog()), de
             * VRAIS retours a la ligne (SAVE_GCODE_STATE ... \n G91 \n ...).
             * Un octet 0x0A brut a l'interieur d'une chaine JSON viole la
             * RFC 8259 (Python json.loads() cote Moonraker le rejetterait) :
             * cJSON echappe correctement au lieu d'un snprintf a la main --
             * meme raison, meme choix que gestion_etat() dans web.c pour un
             * champ qui vient "d'ailleurs que de ce firmware" (voir son
             * commentaire). */
            cJSON *params_obj = cJSON_CreateObject();
            if (params_obj == NULL) {
                JOURNAL_ALERTE(TAG, "construction des parametres gcode impossible (memoire)");
                return ESP_ERR_NO_MEM;
            }
            if (cJSON_AddStringToObject(params_obj, "script", script) == NULL) {
                cJSON_Delete(params_obj);
                JOURNAL_ALERTE(TAG, "construction des parametres gcode impossible (memoire)");
                return ESP_ERR_NO_MEM;
            }
            char *params = cJSON_PrintUnformatted(params_obj);
            cJSON_Delete(params_obj);
            if (params == NULL) {
                JOURNAL_ALERTE(TAG, "impression JSON des parametres gcode impossible");
                return ESP_FAIL;
            }
            bool succes = false;
            char erreur[128];
            erreur[0] = '\0';
            esp_err_t erreur_ws = moonraker_ws_commande("printer.gcode.script", params,
                                                          MOONRAKER_WS_COMMANDE_TIMEOUT_MS, &succes, erreur,
                                                          sizeof(erreur));
            cJSON_free(params);
            if (erreur_ws != ESP_OK) {
                JOURNAL_ALERTE(TAG, "commande gcode (WS) en echec (%s)", esp_err_to_name(erreur_ws));
                return erreur_ws;
            }
            /* Meme limite protocolaire que la macro ci-dessus (voir son
             * commentaire LIMITE PROTOCOLAIRE) : `succes` capture les rejets
             * JSON-RPC de PROTOCOLE, pas un gcode invalide accepte puis
             * refuse par Klipper via notify_gcode_response. */
            if (!succes) {
                JOURNAL_ALERTE(TAG, "gcode refuse par Moonraker (%s)", erreur);
                return ESP_FAIL;
            }
            JOURNAL_INFO(TAG, "commande gcode -> WS printer.gcode.script");
            return ESP_OK;
        }

        if (est_spoolman) {
            /* Feature Spoolman : copie stricte du bloc power ci-dessous --
             * `arguments_json` relaye tel quel, pas de repli HTTP (WS hors
             * ligne tombe dans la table generique, qui ne connait pas
             * "spoolman" -> ESP_ERR_NOT_SUPPORTED, refus honnete plutot
             * qu'un silence). */
            bool succes = false;
            char erreur[128];
            erreur[0] = '\0';
            /* post_spool_id, PAS spool_id (constate sur machine reelle le
             * 2026-08-15) : Moonraker prefixe le VERBE au dernier segment
             * quand un endpoint accepte plusieurs verbes (voir
             * APIDefinition.create() dans moonraker/common.py).
             * /server/spoolman/spool_id etant GET|POST, les methodes JSON-RPC
             * sont get_spool_id et post_spool_id -- "server.spoolman.spool_id"
             * n'existe pas et rendait -32601 "Method not found". Les endpoints
             * a verbe UNIQUE (status, proxy) gardent leur nom tel quel. */
            esp_err_t erreur_ws = moonraker_ws_commande("server.spoolman.post_spool_id", arguments_json,
                                                          MOONRAKER_WS_COMMANDE_TIMEOUT_MS, &succes,
                                                          erreur, sizeof(erreur));
            if (erreur_ws != ESP_OK) {
                JOURNAL_ALERTE(TAG, "commande spoolman (WS) en echec (%s)", esp_err_to_name(erreur_ws));
                return erreur_ws;
            }
            if (!succes) {
                JOURNAL_ALERTE(TAG, "spoolman refusee par Moonraker (%s)", erreur);
                return ESP_FAIL;
            }
            return ESP_OK;
        }

        if (est_power) {
            /* Feature "Power devices Moonraker", tache B : `arguments_json`
             * relaye TEL QUEL comme params_json -- voir le commentaire de
             * est_power ci-dessus. Pas de repli HTTP (contrairement a
             * macro/gcode ci-dessus) : hors perimetre de cette tache/de la
             * spec (design doc, section "Envoi de la bascule" -- tout passe
             * par moonraker_ws_commande()) ; WS hors ligne fait tomber ce
             * chemin dans la table generique plus bas
             * (moonraker_chemin_commande() ne connait pas "power" ->
             * ESP_ERR_NOT_SUPPORTED, comportement honnete plutot qu'un
             * silence). */
            bool succes = false;
            char erreur[128];
            erreur[0] = '\0';
            /* post_device, PAS set_device (audit du 2026-08-15 contre un vrai
             * Moonraker : "machine.device_power.set_device" n'existe pas, il
             * rendait -32601). L'endpoint est /machine/device_power/device,
             * GET|POST -> methodes get_device et post_device ; meme regle de
             * prefixe que pour Spoolman juste au-dessus. Les params
             * ({"device":..,"action":"toggle"}) etaient deja les bons. */
            esp_err_t erreur_ws = moonraker_ws_commande("machine.device_power.post_device", arguments_json,
                                                          MOONRAKER_WS_COMMANDE_TIMEOUT_MS, &succes, erreur,
                                                          sizeof(erreur));
            if (erreur_ws != ESP_OK) {
                JOURNAL_ALERTE(TAG, "commande power (WS) en echec (%s)", esp_err_to_name(erreur_ws));
                return erreur_ws;
            }
            if (!succes) {
                JOURNAL_ALERTE(TAG, "power refusee par Moonraker (%s)", erreur);
                return ESP_FAIL;
            }
            JOURNAL_INFO(TAG, "commande power -> WS machine.device_power.post_device");
            return ESP_OK;
        }

        /* Jalon 3a, tache 5 : RPC corrolee plutot qu'un POST -- le resultat
         * REEL de Klipper (accepte/refuse par le protocole JSON-RPC) revient
         * ici, pas juste "HTTP 200" (spec §4). `arguments_json` n'est PAS
         * utilise : les quatre actions connues de rpc_methode_commande()
         * n'en prennent jamais (voir backend.h). */
        (void)arguments_json;

        char methode[RPC_METHODE_COMMANDE_MAX];
        if (!rpc_methode_commande(action, methode, sizeof(methode))) {
            JOURNAL_ALERTE(TAG, "commande inconnue %s", action);
            return ESP_ERR_NOT_SUPPORTED;
        }

        bool succes = false;
        char erreur[128];
        erreur[0] = '\0';
        esp_err_t erreur_ws = moonraker_ws_commande(methode, NULL, MOONRAKER_WS_COMMANDE_TIMEOUT_MS,
                                                      &succes, erreur, sizeof(erreur));
        if (erreur_ws != ESP_OK) {
            JOURNAL_ALERTE(TAG, "commande WS %s en echec (%s)", action, esp_err_to_name(erreur_ws));
            return erreur_ws;
        }
        if (!succes) {
            JOURNAL_ALERTE(TAG, "commande %s refusee par Moonraker (%s)", action, erreur);
            return ESP_FAIL;
        }
        JOURNAL_INFO(TAG, "commande %s -> WS %s", action, methode);
        return ESP_OK;
    }

    if (est_macro) {
        /* Repli HTTP (spec §4) : POST /printer/gcode/script?script=<nom> --
         * l'endpoint REST documente de Moonraker pour printer.gcode.script,
         * meme convention "?script=" que Fluidd/Mainsail/KlipperScreen. Nom
         * de macro NON echappe dans la requete de la query string (meme
         * choix que le reste de ce seam -- factice_extraire_nom_macro() dans
         * backend_factice.c, rpc_lire_macros() dans moonraker_rpc.c -- ni
         * l'un ni l'autre n'echappe non plus) : un nom Klipper valide
         * ("gcode_macro NOM") est alphanumerique + underscore, jamais
         * d'espace ni de caractere reserve URL. */
        char chemin[MOONRAKER_MACRO_NOM_MAX + 32];
        int ecrit = snprintf(chemin, sizeof(chemin), "printer/gcode/script?script=%s", nom_macro);
        if (ecrit < 0 || (size_t)ecrit >= sizeof(chemin)) {
            JOURNAL_ALERTE(TAG, "construction du chemin macro impossible pour %s", nom_macro);
            return ESP_FAIL;
        }
        JOURNAL_INFO(TAG, "commande macro %s -> POST /%s", nom_macro, chemin);
        return moonraker_requete(HTTP_METHOD_POST, chemin, NULL);
    }

    if (est_gcode) {
        /* Tache 1, jalon 3b : meme endpoint que la macro ci-dessus
         * (POST /printer/gcode/script?script=...), mais ECART DELIBERE sur
         * l'echappement : le nom de macro ci-dessus n'en a jamais besoin
         * (alphanumerique + underscore uniquement), alors qu'un script
         * gcode contient couramment des espaces et, pour un jog, de vrais
         * retours a la ligne -- ni l'un ni l'autre n'est valide tel quel
         * dans une query string ni dans une ligne de requete HTTP (voir le
         * commentaire de moonraker_url_encoder_script() ci-dessus pour le
         * detail). Codage pourcentage donc obligatoire ici, contrairement
         * au reste de ce seam. */
        char script_code[(KLIPPER_GCODE_MAX * 3) + 1];
        if (!moonraker_url_encoder_script(script, script_code, sizeof(script_code))) {
            JOURNAL_ALERTE(TAG, "codage URL du script gcode impossible (trop long)");
            return ESP_FAIL;
        }
        char chemin_gcode[sizeof(script_code) + 32];
        int ecrit = snprintf(chemin_gcode, sizeof(chemin_gcode), "printer/gcode/script?script=%s", script_code);
        if (ecrit < 0 || (size_t)ecrit >= sizeof(chemin_gcode)) {
            JOURNAL_ALERTE(TAG, "construction du chemin gcode impossible");
            return ESP_FAIL;
        }
        JOURNAL_INFO(TAG, "commande gcode -> POST /%s", chemin_gcode);
        return moonraker_requete(HTTP_METHOD_POST, chemin_gcode, NULL);
    }

    (void)arguments_json; /* aucune des quatre actions ci-dessous ne prend de corps en HTTP non plus */

    /* Correspondance action -> chemin extraite en fonction pure (revue tache 9)
     * pour etre testable sans reseau, voir moonraker_chemin_commande() et
     * host-test/tests/test_commandes.c. */
    char chemin[MOONRAKER_CHEMIN_COMMANDE_MAX];
    if (!moonraker_chemin_commande(action, chemin, sizeof(chemin))) {
        /* Une action inconnue doit echouer fort et explicitement, pour que
         * l'interface puisse griser un bouton en connaissant la raison —
         * jamais l'ignorer en silence (meme regle que backend_factice.c). */
        JOURNAL_ALERTE(TAG, "commande inconnue %s", action);
        return ESP_ERR_NOT_SUPPORTED;
    }

    JOURNAL_INFO(TAG, "commande %s -> POST /%s", action, chemin);
    return moonraker_requete(HTTP_METHOD_POST, chemin, NULL);
}

/* Jalon 3a, tache 5 : 250 ms quand le WS est en ligne (le drain est quasi
 * gratuit, voir spec §4), 1000 ms en repli HTTP -- INCHANGE par rapport au
 * jalon 2a. `etat` n'est pas utilise : l'etat en ligne/hors ligne du WS est
 * porte en statique par moonraker_ws.c (g_connecte), pas relu depuis le
 * tampon d'etat (meme raison que g_actif ci-dessus, jamais relu depuis
 * `etat` -- voir le contrat de backend.h sur ce champ). */
static uint32_t backend_moonraker_periode_ms(void *etat)
{
    (void)etat;
    return moonraker_ws_en_ligne() ? 250u : 1000u;
}

static const backend_desc_t g_backend_moonraker_desc = {
    .nom = "moonraker",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = backend_moonraker_demarrer,
    .rafraichir = backend_moonraker_rafraichir,
    .arreter = backend_moonraker_arreter,
    .commande = backend_moonraker_commande,
    .periode_ms = backend_moonraker_periode_ms,
};

const backend_desc_t *backend_moonraker_desc(void)
{
    return &g_backend_moonraker_desc;
}
