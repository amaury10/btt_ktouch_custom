/* Point d'entrée du simulateur : construit l'habillage (tâche 4 — barre
 * d'état, bandeau de notifications) et l'écran de départ -- ECRAN_ACCUEIL
 * (tâche 6) ou ECRAN_CONFIGURATION (tâche 8, --scenario 7/8, voir plus bas) --
 * fait tourner la boucle simulée (source_etat_sim.c) contre backend_factice
 * (ou un backend qui échoue toujours, pour démontrer l'état dégradé/hors
 * ligne), puis soit capture le tout en PNG, soit l'affiche dans une fenêtre
 * SDL interactive.
 *
 * C'est ici que la chaîne complète tourne pour la première fois : backend
 * factice -> boucle_cycle() -> magasin d'état -> génération -> ECRAN_ACCUEIL
 * (voir apps/klipper/ecrans/ecran_accueil.h). Jusqu'à la tâche 5, ce fichier
 * assemblait un écran jouet local (ECRAN_DEMO) pour prouver que l'habillage
 * transmet réellement etat/generation/liaison à un écran empilé ; la tâche 6
 * le remplace par l'écran réel plutôt que de garder les deux, exactement
 * comme ce fichier avait lui-même remplacé la mire de vérification de la
 * tâche 1 (revue jalon 2b) — un écran jouet qui a rempli son rôle une
 * fois ne mérite pas un second indicateur de ligne de commande pour choisir
 * entre lui et l'écran réel.
 *
 * Tâche 11 : `--app jouet` bascule ce même point d'entrée sur
 * exemples/backend_jouet/ (backend_jouet.c + ecran_jouet.c) au lieu de
 * l'application Klipper -- l'assemblage, ici, est le SEUL endroit que la
 * tâche 11 autorise à toucher ; ni core/ ni ui/ n'ont changé une ligne
 * (vérification littérale faite à la revue de la tâche 11, jalon 2b). Un
 * détail EST resté
 * hors de portée de ce fichier : habillage_pomper() (ui/habillage.c) porte
 * un tampon `etat_klipper_t` concret pour relayer etat/génération/liaison à
 * navigation_mettre_a_jour() -- lire son commentaire de tête, qui documente
 * ce couplage comme le second site qu'un fork adapte. Avec un backend dont
 * l'état ne fait pas exactement sizeof(etat_klipper_t) octets,
 * ui_etat_instantane() interne à habillage_pomper() échoue sa vérification
 * de taille (voir source_etat_sim.c) et rend disponible=false : la barre
 * d'état continue de se rafraîchir (titre, heure, wifi, batterie — aucun ne
 * dépend de la taille de l'état applicatif), mais SA PASTILLE DE LIAISON
 * reste figée sur "connecting"/gris, et surtout navigation_mettre_a_jour()
 * n'est jamais appelée par habillage_pomper() lui-même pour cet écran-là.
 * jouet_pomper() ci-dessous contourne ce SEUL défaut sans toucher ui/ : il
 * appelle lui-même ui_etat_instantane() (façade générique, pointeur + taille,
 * voir ui/source_etat.h) avec un tampon etat_jouet_t, calcule
 * donnees_perimees via habillage_donnees_perimees() (fonction pure, publique,
 * elle aussi générique) et relaie directement à navigation_mettre_a_jour()
 * (également générique, void *etat, voir ui/navigation.h) -- trois appels
 * publics que habillage_pomper() fait déjà lui-même en interne, simplement
 * réassemblés ici pour un état de taille différente. Reste néanmoins un vrai
 * défaut du socle pour un fork non-Klipper : une pastille de connexion
 * fausse est un vrai regret, documenté à la revue de la tâche 11, jalon 2b. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "afficheur.h"
#include "lvgl.h"

#include "accueil_choix.h"
#include "backend.h"
#include "backend_factice.h"
#include "backend_jouet.h"
#include "clavier.h"
#include "confirmation.h"
#include "ecran_accueil.h"
#include "ecran_accueil_hub.h"
#include "ecran_actions.h"
#include "ecran_configuration.h"
#include "ecran_console.h"
#include "ecran_deplacer.h"
#include "ecran_extruder.h"
#include "ecran_fichiers.h"
#include "ecran_homing.h"
#include "ecran_jouet.h"
#include "ecran_limites.h"
#include "ecran_macros.h"
#include "ecran_menu_reglages.h"
#include "ecran_niveau_lit.h"
#include "ecran_power.h"
#include "ecran_reglage_fin.h"
#include "ecran_reglages_wifi.h"
#include "ecran_retraction.h"
#include "ecran_stub.h"
#include "ecran_temperatures.h"
#include "ecran_updater.h"
#include "ecran_ventilateurs.h"
#include "ecran_zcalibrate.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "hote_parse.h"
#include "klipper_temp_historique.h"
#include "moonraker_pc.h"
#include "navigation.h"
#include "source_etat.h"
#include "source_etat_sim.h"

typedef enum {
    APP_ACCUEIL = 0, /* Klipper (comportement par defaut, inchange) */
    APP_JOUET,       /* tache 11 : exemples/backend_jouet/ */
} app_t;

/* --- Backend qui échoue systématiquement -------------------------------- *
 * Sert uniquement à démontrer, dans le simulateur, l'état dégradé/hors
 * ligne de l'habillage : backend_factice.c ne produit jamais d'échec (voir
 * son en-tête), donc rien ne fait naturellement progresser liaison_t au-delà
 * de LIAISON_EN_LIGNE. Ce backend-jouet, local à ce fichier, ne touche
 * jamais le réseau — il rend ESP_FAIL immédiatement — et laisse
 * boucle_cycle()/liaison.c (le vrai code, pas une simulation de leur effet)
 * faire progresser la liaison vers DEGRADEE (3 échecs) puis HORS_LIGNE (10
 * échecs) exactement comme le ferait un hôte injoignable sur cible. */
static esp_err_t echec_demarrer(void *etat, const backend_hote_t *hote)
{
    (void)hote;
    memset(etat, 0, sizeof(etat_klipper_t));
    return ESP_OK;
}

static esp_err_t echec_rafraichir(void *etat)
{
    (void)etat;
    return ESP_FAIL;
}

static void echec_arreter(void *etat)
{
    (void)etat;
}

static esp_err_t echec_commande(void *etat, const char *action, const char *arguments_json)
{
    (void)etat;
    (void)action;
    (void)arguments_json;
    return ESP_ERR_NOT_SUPPORTED;
}

static const backend_desc_t BACKEND_ECHEC_DESC = {
    .nom = "echec-demo",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = echec_demarrer,
    .rafraichir = echec_rafraichir,
    .arreter = echec_arreter,
    .commande = echec_commande,
};

/* --- Backend qui échoue systématiquement, variante jouet (tâche 11) -------
 * Même rôle que BACKEND_ECHEC_DESC ci-dessus (démontrer l'état dégradé/hors
 * ligne pour --app jouet --echec), mais dimensionné sur etat_jouet_t : la
 * struct ci-dessus ne peut PAS être réutilisée telle quelle, elle est câblée
 * en dur sur sizeof(etat_klipper_t) (son .taille_etat, et le memset() de
 * echec_demarrer()) -- un fork qui voudrait la partager entre deux
 * applications devrait déjà la paramétrer sur une taille, ce n'était pas fait
 * avant la tâche 11. Dupliquée ici plutôt que généralisée : quatre petites
 * fonctions locales à ce fichier, plus simple à lire qu'une indirection pour
 * un unique appelant. */
static esp_err_t jouet_echec_demarrer(void *etat, const backend_hote_t *hote)
{
    (void)hote;
    memset(etat, 0, sizeof(etat_jouet_t));
    return ESP_OK;
}

static esp_err_t jouet_echec_rafraichir(void *etat)
{
    (void)etat;
    return ESP_FAIL;
}

static void jouet_echec_arreter(void *etat)
{
    (void)etat;
}

static esp_err_t jouet_echec_commande(void *etat, const char *action, const char *arguments_json)
{
    (void)etat;
    (void)action;
    (void)arguments_json;
    return ESP_ERR_NOT_SUPPORTED;
}

static const backend_desc_t BACKEND_JOUET_ECHEC_DESC = {
    .nom = "jouet-echec-demo",
    .taille_etat = sizeof(etat_jouet_t),
    .demarrer = jouet_echec_demarrer,
    .rafraichir = jouet_echec_rafraichir,
    .arreter = jouet_echec_arreter,
    .commande = jouet_echec_commande,
};

/* Relaie l'état du backend jouet au sommet de la pile de navigation --
 * l'équivalent, pour --app jouet, de ce que habillage_pomper() fait déjà en
 * interne pour etat_klipper_t (voir son commentaire dans ui/habillage.c) :
 * ui_etat_instantane() est la façade générique de ui/source_etat.h (void*,
 * taille), habillage_donnees_perimees() et navigation_mettre_a_jour() sont
 * elles aussi des fonctions publiques génériques de ui/ -- rien ici n'entre
 * dans core/ ni dans ui/. Voir le commentaire de tête de ce fichier pour
 * pourquoi ce relais existe séparément de habillage_pomper() plutôt que de
 * passer par lui. */
static void jouet_pomper(void)
{
    etat_jouet_t etat;
    uint32_t generation = 0;
    liaison_etat_t liaison = LIAISON_CONNEXION;
    if (ui_etat_instantane(&etat, sizeof(etat), &generation, &liaison)) {
        navigation_mettre_a_jour(&etat, habillage_donnees_perimees(liaison));
    }
}

/* Tache 2 (graphes de temperature) : sur cible, un lv_timer independant
 * (app_main.c, echantillon_temp_cb()) pousse un point d'historique toutes
 * les 5 s, tourne en continu sur le fil LVGL. Ce simulateur n'a ni ce
 * timer ni ce fil : en mode capture rien ne tourne apres le dernier
 * cycle, et en mode fenetre la boucle plus bas est deja le seul "fil"
 * disponible. On reproduit le meme echantillonnage ici en comptant les
 * cycles simules (1 cycle = source_etat_sim_cycle() = ~1 s simulee, voir
 * le commentaire de la boucle de capture plus bas) : un point tous les 5
 * cycles, pour que --ecran accueil (tache 3) montre une courbe qui se
 * remplit sans dependre d'un outil externe. */
static int g_cycles_depuis_echantillon_temp = 0;

static void echantillon_temp_sim(void)
{
    etat_klipper_t e;
    if (ui_etat_instantane(&e, sizeof(e), NULL, NULL)) {
        klipper_temp_historique_pousser(&e);
    }
}

/* Enveloppe source_etat_sim_cycle() pour y greffer l'echantillonnage
 * ci-dessus, aux TROIS sites qui font avancer la boucle simulee (amorce,
 * boucle de capture, boucle fenetre) -- jamais pour --app jouet :
 * etat_jouet_t n'a pas la taille de etat_klipper_t (voir jouet_pomper()
 * ci-dessus), ui_etat_instantane() y rendrait simplement faux, garde
 * explicite ici pour ne jamais compter sur cet echec silencieux. */
static void cycle_simule_avec_echantillon(app_t app)
{
    source_etat_sim_cycle();
    if (app != APP_ACCUEIL) {
        return;
    }
    if (++g_cycles_depuis_echantillon_temp >= 5) {
        g_cycles_depuis_echantillon_temp = 0;
        echantillon_temp_sim();
    }
}

/* --- Démonstration du clavier modal et du dialogue de confirmation --------
 * (tâche 7) : --scenario 5/6 ci-dessous, en mode capture uniquement. Ces
 * rappels ne sont jamais invoqués par une capture hors écran (rien n'y
 * simule un appui tactile, voir --scenario ci-dessous et host-test/tests/
 * test_clavier.c pour la façon dont les événements sont simulés côté tests) ;
 * ils n'existent que pour satisfaire la signature de clavier_ouvrir()/
 * confirmation_ouvrir(), qui refusent un rappel NULL (voir clavier.h).
 *
 * --hote <adresse:port> (tâche 7 du JALON 3a -- numérotation de tâche
 * distincte de celle ci-dessus, propre au jalon 2b) bascule --app accueil
 * sur moonraker_pc.c (backend HTTP réel, sockets POSIX nus, voir son
 * en-tête) au lieu de backend_factice.c -- la comparaison différée depuis
 * le jalon 2a, enfin exécutée sur PC. Analysé avec hote_parse()
 * (core/hote_parse.c, déjà lié ici pour ecran_configuration.c), jamais un
 * second analyseur. Ignoré pour --app jouet (le jouet démontre le fork
 * non-Klipper, aucune notion d'hôte réseau) et incompatible avec
 * --scenario/--echec (propres au backend factice) -- voir plus bas où ce
 * choix de backend est fait. */
static void demo_clavier_rappel(const char *valeur, void *contexte)
{
    (void)valeur;
    (void)contexte;
}

static void demo_confirmation_rappel(bool confirme, void *contexte)
{
    (void)confirme;
    (void)contexte;
}

/* Chooser de l'écran de fond injecté dans l'habillage (bascule vivante
 * repos<->impression, sous-projet 5 tâche 2) : le MÊME comportement que
 * choix_accueil_klipper() dans firmware/main/app_main.c -- hub au repos,
 * accueil impression pendant une impression, via accueil_impression_actif()
 * (accueil_choix.h). Enregistré ci-dessous pour que le simulateur exerce la
 * bascule : le fond suit désormais l'état simulé (backend_factice scénarios)
 * une fois la boucle démarrée, pas seulement le choix figé au démarrage.
 * L'état arrive opaque de l'habillage générique ; ce point d'entrée
 * (l'assemblage applicatif) le recaste vers etat_klipper_t. */
static const ecran_desc_t *choix_accueil_klipper(const void *etat, void *ctx)
{
    (void)ctx;
    return accueil_impression_actif((const etat_klipper_t *)etat) ? &ECRAN_ACCUEIL
                                                                  : &ECRAN_ACCUEIL_HUB;
}

int main(int argc, char **argv)
{
    const char *chemin_capture = NULL;
    int cycles = 5;
    /* Scenario par defaut : 0 (repos), pas 1 (impression) -- demande de
     * l'utilisateur : une machine qui demarre doit se presenter au repos,
     * comme le ferait l'appareil reel a cote d'une imprimante inactive ;
     * l'impression se demande explicitement (--scenario 1). */
    int scenario = 0;
    bool echec = false;
    app_t app = APP_ACCUEIL;
    /* Tache 6 (jalon 3a) : empile ECRAN_MACROS par-dessus ECRAN_ACCUEIL
     * (--ecran macros), et lance eventuellement une macro nommee avant la
     * capture (--macro <nom>) -- le pendant, en mode capture, d'un tap reel
     * sur un bouton de la grille (rien ne simule le tactile en mode capture,
     * voir le commentaire de tete de ce fichier). */
    const ecran_desc_t *ecran_demande = NULL; /* --ecran <nom> : ecran empile par-dessus l'accueil pour la capture */
    const char *macro_a_lancer = NULL;
    /* Tache 7 (jalon 3a) : --hote <adresse:port>, voir le commentaire pres
     * de demo_clavier_rappel() plus haut. */
    const char *hote_brut = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            chemin_capture = argv[++i];
        } else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--echec") == 0) {
            echec = true;
        } else if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) {
            /* Tache 11 : "accueil" (defaut, Klipper) ou "jouet"
             * (exemples/backend_jouet/) -- toute autre valeur retombe sur
             * "accueil" plutot que d'echouer, meme politique defensive que
             * --scenario pour un numero inconnu. */
            const char *valeur = argv[++i];
            app = (strcmp(valeur, "jouet") == 0) ? APP_JOUET : APP_ACCUEIL;
        } else if (strcmp(argv[i], "--ecran") == 0 && i + 1 < argc) {
            /* Ecran empile PAR-DESSUS l'accueil pour la capture (le pendant, en
             * mode capture, d'un tap reel sur une case de menu -- rien ne simule
             * le tactile en capture). Valeurs reconnues : "macros", "deplacer",
             * "temperatures", "extruder", "ventilateurs", "fichiers", "wifi",
             * "fin", "zcal", "lit", "limites", "retraction", "menu" (le
             * sous-menu Configuration, tache 8 -- ECRAN_MENU_REGLAGES, PAS
             * ECRAN_CONFIGURATION, voir ecran_menu_reglages.h pour la note de
             * collision de noms), "power", "bed_mesh" (alias "bedmesh"),
             * "input_shaper" (alias "shaper"), "spoolman", "console" -- ces
             * cinq derniers sont les stubs restants de la tache 7
             * (ecran_stub.h, backend absent) ; "updater" est desormais
             * l'ecran d'etat reel de Task 2 (jalon OTA firmware,
             * ecran_updater.h) -- "actions" (le sous-menu
             * Actions, refonte IHM KlipperScreen tache 2 -- ECRAN_ACTIONS,
             * voir ecran_actions.h), "homing" (le panneau Homing, refonte
             * IHM KlipperScreen tache 3 -- ECRAN_HOMING, voir ecran_homing.h)
             * -- toute autre retombe sur l'accueil seul (ecran_demande reste
             * NULL), meme politique defensive que --app. */
            const char *valeur = argv[++i];
            if (strcmp(valeur, "macros") == 0)            ecran_demande = &ECRAN_MACROS;
            else if (strcmp(valeur, "deplacer") == 0)     ecran_demande = &ECRAN_DEPLACER;
            else if (strcmp(valeur, "temperatures") == 0) ecran_demande = &ECRAN_TEMPERATURES;
            else if (strcmp(valeur, "extruder") == 0)     ecran_demande = &ECRAN_EXTRUDER;
            else if (strcmp(valeur, "ventilateurs") == 0) ecran_demande = &ECRAN_VENTILATEURS;
            else if (strcmp(valeur, "fichiers") == 0)     ecran_demande = &ECRAN_FICHIERS;
            else if (strcmp(valeur, "wifi") == 0)         ecran_demande = &ECRAN_REGLAGES_WIFI;
            else if (strcmp(valeur, "fin") == 0)          ecran_demande = &ECRAN_REGLAGE_FIN;
            else if (strcmp(valeur, "zcal") == 0)         ecran_demande = &ECRAN_ZCALIBRATE;
            else if (strcmp(valeur, "lit") == 0)          ecran_demande = &ECRAN_NIVEAU_LIT;
            else if (strcmp(valeur, "limites") == 0)      ecran_demande = &ECRAN_LIMITES;
            else if (strcmp(valeur, "retraction") == 0)   ecran_demande = &ECRAN_RETRACTION;
            else if (strcmp(valeur, "menu") == 0)         ecran_demande = &ECRAN_MENU_REGLAGES;
            else if (strcmp(valeur, "power") == 0)        ecran_demande = &ECRAN_POWER;
            else if (strcmp(valeur, "bed_mesh") == 0
                     || strcmp(valeur, "bedmesh") == 0)   ecran_demande = &ECRAN_BED_MESH;
            else if (strcmp(valeur, "input_shaper") == 0
                     || strcmp(valeur, "shaper") == 0)    ecran_demande = &ECRAN_INPUT_SHAPER;
            else if (strcmp(valeur, "spoolman") == 0)     ecran_demande = &ECRAN_SPOOLMAN;
            else if (strcmp(valeur, "updater") == 0)      ecran_demande = &ECRAN_UPDATER;
            else if (strcmp(valeur, "console") == 0)      ecran_demande = &ECRAN_CONSOLE;
            else if (strcmp(valeur, "actions") == 0)      ecran_demande = &ECRAN_ACTIONS;
            else if (strcmp(valeur, "homing") == 0)       ecran_demande = &ECRAN_HOMING;
        } else if (strcmp(argv[i], "--macro") == 0 && i + 1 < argc) {
            macro_a_lancer = argv[++i];
        } else if (strcmp(argv[i], "--hote") == 0 && i + 1 < argc) {
            hote_brut = argv[++i];
        }
    }

    afficheur_mode_t mode = (chemin_capture != NULL) ? AFFICHEUR_HORS_ECRAN : AFFICHEUR_FENETRE;
    if (!afficheur_demarrer(mode)) {
        fprintf(stderr, "echec du demarrage de l'afficheur (mode %s)\n",
                mode == AFFICHEUR_FENETRE ? "fenetre" : "hors ecran");
        return 1;
    }

    lv_obj_t *racine = lv_screen_active();
    habillage_construire(racine);

    /* Tache 3 (jalon 3b) : choix du backend et demarrage de la boucle
     * simulee DEPLACES ICI, avant l'empilement de tout ecran d'accueil --
     * la topologie precedente empilait ECRAN_ACCUEIL avant meme de choisir
     * un backend. accueil_choix.h (accueil_impression_actif()) a besoin
     * d'un premier etat REEL pour trancher entre ECRAN_ACCUEIL (impression)
     * et ECRAN_ACCUEIL_HUB (repos) ; seul le backend, une fois demarre,
     * peut le fournir -- voir le commentaire du cycle d'amorce plus bas. */
    const backend_desc_t *backend;
    if (app == APP_JOUET) {
        backend = echec ? &BACKEND_JOUET_ECHEC_DESC : backend_jouet_desc();
    } else if (hote_brut != NULL) {
        /* Tache 7 (jalon 3a) : --hote prend le pas sur --scenario/--echec,
         * qui n'ont de sens que contre backend_factice.c. hote_parse() est
         * la MEME fonction pure que ecran_configuration.c utilise pour
         * valider une saisie -- une chaine que l'ecran de configuration
         * refuserait n'a pas de raison d'etre acceptee ici. moonraker_pc_
         * definir_hote() DOIT etre appelee avant source_etat_sim_demarrer()
         * (voir moonraker_pc.h) : c'est le seul canal, cote simulateur, par
         * lequel un hote reel atteint ce backend. */
        backend_hote_t hote;
        if (!hote_parse(hote_brut, &hote)) {
            fprintf(stderr, "--hote : adresse invalide (%s)\n", hote_brut);
            afficheur_arreter();
            return 1;
        }
        moonraker_pc_definir_hote(&hote);
        backend = moonraker_pc_desc();
    } else {
        backend = echec ? &BACKEND_ECHEC_DESC : backend_factice_desc();
        if (!echec) {
            backend_factice_scenario(scenario);
        }
    }
    if (!source_etat_sim_demarrer(backend)) {
        fprintf(stderr, "echec du demarrage de la boucle simulee\n");
    }

    /* Tache 3 (jalon 3b) : un premier cycle "d'amorce", pour que l'etat lu
     * juste apres ne soit plus celui, entierement nul, que demarrer() vient
     * d'ecrire -- sans lui, accueil_impression_actif() trancherait TOUJOURS
     * pour l'accueil idle, y compris avec --scenario 1 (impression), ce qui
     * ne prouverait jamais ECRAN_ACCUEIL depuis ce fichier. Sautee dans
     * exactement un cas : capture avec --cycles 0, ou l'appelant demande
     * explicitement l'etat BRUT d'avant tout cycle (voir la branche
     * `cycles == 0` plus bas) -- l'etat y est alors entierement nul, ce qui
     * fait deja trancher accueil_impression_actif() pour l'accueil idle, un
     * choix honnete pour "avant que la machine n'ait rien rapporte", sans
     * avoir besoin de ce cycle. Jamais pour --app jouet, qui n'a qu'un seul
     * ecran (ECRAN_JOUET) et aucune distinction idle/impression a trancher.
     * Compte comme le premier des `cycles` demandes par --cycles (voir la
     * boucle de capture plus bas, qui n'en refait que cycles-1) : le
     * contrat existant de cette option ("avance la boucle simulee d'autant
     * de secondes") reste exact, jamais cycles+1. */
    bool amorce_faite = false;
    if (app == APP_ACCUEIL && !(chemin_capture != NULL && cycles == 0)) {
        cycle_simule_avec_echantillon(app);
        amorce_faite = true;
    }

    bool ecran_config = false;
    if (app == APP_JOUET) {
        /* Tache 11 : ECRAN_JOUET seul, jamais empile avec un accueil
         * Klipper -- les deux applications ne partagent aucun ecran. */
        navigation_empiler(&ECRAN_JOUET);
    } else {
        /* Tache 3 (jalon 3b), mis a jour tache 7 (accueil-hub remplace
         * l'idle) : le choix hub/impression, calcule sur l'etat que le
         * cycle d'amorce ci-dessus vient de rendre disponible (ou sur
         * l'etat nul de depart si cette amorce a ete sautee, voir son
         * commentaire) -- accueil_impression_actif() est le MEME helper pur
         * qu'appellera un futur app_main.c pour la bascule vivante (differee,
         * voir task-3-brief.md), exerce ici pour de vrai des le demarrage :
         * un `--scenario 1` demarre donc sur ECRAN_ACCUEIL, un `--scenario 0`
         * (ou tout scenario "repos", 10/11/12) sur ECRAN_ACCUEIL_HUB, ce qui
         * prouve les DEUX ecrans et le helper de choix depuis ce seul
         * fichier. ui_etat_instantane() rendant faux (boucle pas demarree,
         * echec de source_etat_sim_demarrer() ci-dessus) retombe sur
         * l'accueil-hub -- le choix le plus sur, meme politique que
         * app_main.c (voir son commentaire). */
        etat_klipper_t etat_amorce;
        uint32_t generation_amorce = 0;
        liaison_etat_t liaison_amorce = LIAISON_CONNEXION;
        bool impression = false;
        if (ui_etat_instantane(&etat_amorce, sizeof(etat_amorce), &generation_amorce, &liaison_amorce)) {
            impression = accueil_impression_actif(&etat_amorce);
        }
        navigation_empiler(impression ? &ECRAN_ACCUEIL : &ECRAN_ACCUEIL_HUB);

        /* Bascule vivante repos<->impression du fond (sous-projet 5, tâche 2) :
         * enregistrée UNIQUEMENT pour l'application Klipper (--app accueil),
         * jamais pour --app jouet dont l'état (etat_jouet_t) n'est pas un
         * etat_klipper_t et qui n'a de toute façon aucune distinction
         * idle/impression. À partir d'ici, chaque habillage_pomper() (celui de
         * l'amorce ci-dessous, puis ceux des boucles de capture/fenêtre) fait
         * suivre le fond à l'état simulé quand la pile est à profondeur 1 --
         * exactement ce que fait app_main.c sur cible. */
        habillage_definir_choix_accueil(choix_accueil_klipper, NULL);

        /* --scenario 7/8 (tâche 8) : empile ECRAN_CONFIGURATION PAR-DESSUS
         * l'accueil (jamais seul), exactement la topologie que app_main.c
         * construit sur un appareil jamais configuré (reglages_configures()
         * faux) -- voir README §Options pour la numérotation complète. Empiler
         * ECRAN_CONFIGURATION seul (comme ce fichier le faisait avant la revue de
         * la tâche 8, round 1, Q1) rendrait son bouton Save un cul-de-sac :
         * navigation_accueil() est un no-op à profondeur 1, Save n'aurait donc
         * aucun endroit où revenir. Ces deux numéros ne correspondent à aucun
         * scénario du backend factice (voir backend_factice_scenario() plus bas,
         * qui les traite comme "tout autre numéro", exactement comme 5/6 déjà). */
        ecran_config = (scenario == 7 || scenario == 8);
        if (ecran_config) {
            navigation_empiler(&ECRAN_CONFIGURATION);
        } else if (ecran_demande != NULL) {
            /* --ecran <nom> : empile l'ecran demande par-dessus l'accueil,
             * jamais combine aux scenarios 7/8 (configuration) dans les
             * captures prevues -- un seul ecran empile par-dessus l'accueil a
             * la fois, meme regle que ci-dessus. */
            navigation_empiler(ecran_demande);
        }
    }

    if (amorce_faite) {
        /* Rend visible l'etat que le cycle d'amorce ci-dessus vient de
         * produire, sur l'ecran qui vient d'etre empile -- sans cet appel,
         * un --cycles 1 ne pomperait jamais (la boucle de capture plus bas
         * ne referait alors AUCUN cycle supplementaire, voir son
         * commentaire), laissant l'ecran dans son etat juste-construit
         * (vide) au moment de la capture. */
        habillage_pomper();
    }

    /* --scenario 9 (tache 9, mode capture uniquement, --app accueil
     * seulement -- backend_factice_commande_echoue() n'a de sens que contre
     * backend_factice_desc()) : demontre l'echec ASYNCHRONE d'une commande --
     * ui_commander() l'accepte tout de suite (ESP_OK), mais son execution
     * reelle, plus tard par la boucle simulee, echoue deliberement
     * (backend_factice_commande_echoue(true), tache 9) -- exactement le
     * chemin qu'un simple appui bouton ne peut pas montrer ici (aucune
     * simulation d'entree tactile en mode capture, voir le commentaire de
     * tete de ce fichier). Empilee AVANT la boucle de cycles ci-dessous : le
     * premier appel a source_etat_sim_cycle() l'execute et la fait echouer,
     * et le habillage_pomper() du meme tour de boucle remonte cet echec au
     * bandeau de notification (voir ui_commande_echec() dans
     * ui/source_etat.h et son polling par habillage_pomper()) -- sans
     * attendre le "host connected" que --cycles positif declenche par
     * ailleurs plus bas (voir la note sur ce bandeau-la). Ne correspond a
     * aucun scenario du backend factice (retombe sur le comportement du
     * scenario 3, "printing" plausible -- voir backend_factice_scenario()) :
     * Pause a un sens contre un etat "printing", meme si aucun champ de cet
     * etat n'influence l'echec force lui-meme. */
    if (app == APP_ACCUEIL && scenario == 9) {
        backend_factice_commande_echoue(true);
        esp_err_t erreur_commande = ui_commander(BACKEND_ACTION_PAUSE, NULL);
        if (erreur_commande != ESP_OK) {
            /* esp_err_to_name() n'existe pas dans shim/esp_err.h (doublure
             * minimale, voir son commentaire de tete) : le code numerique
             * suffit ici, ce n'est qu'un message de diagnostic sur stderr. */
            fprintf(stderr, "scenario 9 : ui_commander a refuse la commande (code %d)\n",
                    (int)erreur_commande);
        }
    }

    /* --macro <nom> (tache 6, mode capture uniquement) : le pendant, en
     * l'absence de tactile simule, du tap qu'un doigt reel enverrait sur un
     * bouton de ECRAN_MACROS -- construit `{"nom":"<nom>"}` avec la MEME
     * fonction pure que ce bouton (ecran_macros_construire_arguments(), voir
     * ecran_macros.h) et empile la commande AVANT la boucle de cycles
     * ci-dessous, meme schema que --scenario 9 juste au-dessus : le premier
     * source_etat_sim_cycle() l'execute (succes ou MACRO_ECHEC selon le nom
     * choisi), et habillage_pomper() du meme tour remonte le resultat au
     * bandeau -- captures scenario 11 (« macro envoyee -> notification
     * succes » et « MACRO_ECHEC -> bandeau rouge »), voir README §Options. */
    if (app == APP_ACCUEIL && macro_a_lancer != NULL) {
        char args[ECRAN_MACROS_ARGUMENTS_MAX];
        if (ecran_macros_construire_arguments(macro_a_lancer, args, sizeof(args))) {
            esp_err_t erreur_commande = ui_commander(BACKEND_ACTION_MACRO, args);
            /* Reprend EXACTEMENT le texte que bouton_macro_cb() (ecran_macros.c)
             * poste au tap reel : cette option EST le pendant de ce tap en
             * l'absence de tactile simule (voir le commentaire ci-dessus),
             * elle doit donc produire la MEME banniere synchrone -- pas
             * seulement empiler la commande en silence. L'echec ASYNCHRONE
             * eventuel (MACRO_ECHEC), lui, arrive plus tard via le seam
             * generique existant (habillage_pomper() dans la boucle de
             * cycles ci-dessous) et remplace celle-ci normalement. */
            char texte[64];
            if (erreur_commande != ESP_OK) {
                fprintf(stderr, "--macro : ui_commander a refuse la commande (code %d)\n",
                        (int)erreur_commande);
                snprintf(texte, sizeof(texte), "Command failed: %s", macro_a_lancer);
                habillage_notifier(texte, true);
            } else {
                snprintf(texte, sizeof(texte), "Macro sent: %s", macro_a_lancer);
                habillage_notifier(texte, false);
            }
        } else {
            fprintf(stderr, "--macro : nom de macro trop long (%s)\n", macro_a_lancer);
        }
    }

    if (chemin_capture != NULL) {
        /* --cycles fait avancer la boucle simulée d'autant de "secondes"
         * avant la capture : un cycle = un rafraîchissement du backend +
         * validation du magasin d'état, exactement ce que ferait
         * boucle_tache() une fois par seconde sur cible (voir
         * source_etat_sim.c). Tache 3 (jalon 3b) : `cycles_restants`, pas
         * `cycles` -- le cycle d'amorce plus haut (`amorce_faite`) en a deja
         * execute un pour choisir l'ecran d'accueil ; en refaire `cycles`
         * ici avancerait la boucle de cycles+1 secondes au lieu des
         * `cycles` demandees (voir le commentaire de l'amorce). */
        int cycles_restants = cycles - (amorce_faite ? 1 : 0);
        for (int i = 0; i < cycles_restants; i++) {
            cycle_simule_avec_echantillon(app);
            habillage_pomper();
            if (app == APP_JOUET) {
                /* Voir le commentaire de tête de ce fichier et celui de
                 * jouet_pomper() : habillage_pomper() ne peut pas relayer
                 * etat_jouet_t lui-même (tampon interne dimensionné sur
                 * etat_klipper_t), ce second appel referme la boucle pour cet
                 * écran-là sans toucher ui/. */
                jouet_pomper();
            }
        }
        if (cycles == 0) {
            habillage_pomper();
            if (app == APP_JOUET) {
                jouet_pomper();
            }
        }
        /* scenario 9 exclu, --macro exclu (tache 6) : la boucle ci-dessus a
         * deja fait remonter, via habillage_pomper(), le bandeau que ce
         * scenario -- ou cette option -- existe pour capturer -- un "host
         * connected" ecrirait par-dessus (habillage_notifier() REMPLACE,
         * jamais n'empile, voir son commentaire dans habillage.h) avant
         * meme la capture. */
        if (cycles > 0 && !(app == APP_ACCUEIL && scenario == 9) &&
            !(app == APP_ACCUEIL && macro_a_lancer != NULL)) {
            habillage_notifier(echec ? "connection lost" : "host connected", echec);
        }

        /* Démonstration tâche 7 : --scenario 5/6 ouvre respectivement le
         * clavier modal et le dialogue de confirmation par-dessus l'écran
         * d'accueil déjà construit ci-dessus, pour que la capture qui suit
         * les montre réellement à l'écran (spec §6 : le plus gros morceau
         * partagé du jalon, jusqu'ici invisible dans le simulateur). Ces
         * numéros ne correspondent à aucun scénario du backend factice (voir
         * backend_factice_scenario() plus haut, qui les a déjà reçus et
         * traités comme "tout autre numéro", README §Options) : le fond
         * derrière les modales est donc celui du scénario 3 (valeurs
         * extrêmes mais plausibles), un arrière-plan quelconque puisque
         * seule la modale elle-même importe pour cette capture. Rien de tout
         * ceci ne s'applique à --app jouet (pas de clavier, pas de
         * configuration a saisir). */
        if (app == APP_ACCUEIL && scenario == 5) {
            clavier_ouvrir("Host address", "192.168.1.42", CLAVIER_TEXTE, demo_clavier_rappel, NULL);
        } else if (app == APP_ACCUEIL && scenario == 6) {
            /* confirmation_ouvrir_ex(), pas confirmation_ouvrir() : reprend
             * EXACTEMENT la copie que le vrai bouton Cancel envoie depuis
             * ecran_accueil.c (fix round 1, revue tache 9, LOW) -- un declin
             * par defaut "Cancel" ferait deux boutons qui commencent tous les
             * deux par le meme mot a cote de l'action "Cancel print". */
            confirmation_ouvrir_ex("Cancel print?",
                                    "This will stop the current print. This cannot be undone.",
                                    "Cancel print", true, "Keep printing", demo_confirmation_rappel, NULL);
        } else if (app == APP_ACCUEIL && scenario == 8) {
            /* Tâche 8 : clavier ouvert par-dessus ECRAN_CONFIGURATION (déjà
             * empilé plus haut, voir `ecran_config`), avec une adresse
             * pré-remplie comme si elle venait d'être saisie -- même
             * technique que le scénario 5 sur l'écran d'accueil : la valeur
             * initiale de clavier_ouvrir() apparaît dans la textarea
             * exactement comme une saisie tactile l'aurait laissée. */
            clavier_ouvrir("Printer address", "192.168.1.42", CLAVIER_TEXTE, demo_clavier_rappel, NULL);
        } else if (app == APP_ACCUEIL && scenario == 13) {
            /* Tâche 5 (jalon 3b), mis à jour tâche 7 (retrait de l'ancien
             * accueil idle) : dialogue de confirmation de homing par-dessus
             * ECRAN_ACCUEIL_HUB (empilé plus haut par la logique
             * hub/impression commune, `backend_factice_scenario(13)` produit
             * le MÊME état repos + axes entièrement référencés que le
             * scénario 10 -- voir son commentaire dans backend_factice.c --
             * pour que "Home X" ait effectivement de quoi confirmer). Comme
             * le scénario 6 ci-dessus pour "Cancel print?" : rien ici ne
             * simule de tactile (voir le commentaire de tête de ce fichier).
             * Avant la tâche 7, cet appel réutilisait les constantes
             * partagées de l'ancien ecran_accueil_idle.c (ECRAN_ACCUEIL_IDLE_
             * HOME_*, mêmes chaînes que le vrai bouton "Home X" de cet
             * écran) pour ne jamais diverger d'une copie tapée à la main --
             * ce fichier a disparu en tâche 7 et ni ECRAN_ACCUEIL_HUB (aucun
             * jog/homing) ni ECRAN_DEPLACER (aucune confirmation avant Home,
             * voir ecran_deplacer.h) n'exposent d'équivalent : ce scénario de
             * démo n'a donc plus de bouton réel à représenter fidèlement,
             * d'où les littéraux ci-dessous (mêmes valeurs que l'ancien
             * ECRAN_ACCUEIL_IDLE_HOME_X/_MESSAGE/_ACTION/_DECLINER).
             *
             * Aucun afficheur_pomper() supplémentaire ici : exactement comme
             * les scénarios 5/6/8 ci-dessus, l'unique pompe plus bas rend d'un
             * coup l'accueil-hub ET le dialogue. Une version de cette tâche
             * avait cru devoir pré-rendre l'écran de fond seul d'abord (le
             * process se bloquait sinon à 100 % de CPU dans lv_tlsf_malloc) :
             * c'était le symptôme d'un pool LVGL trop petit, pas d'un ordre
             * de rendu à respecter -- LV_MEM_SIZE relevé à 256 Ko dans
             * lv_conf.h (voir son commentaire pour le mécanisme exact). Le
             * pré-rendu masquait le blocage mais laissait la modale à 0x0,
             * jamais visible ; il a donc été retiré. */
            confirmation_ouvrir_ex("Home X?", "The axis will move to its endstop.", "Home", true, "Cancel",
                                    demo_confirmation_rappel, NULL);
        } else if (app == APP_ACCUEIL && scenario == 14) {
            /* Tache 6 (jalon 3b), mis a jour tache 7 (retrait de l'ancien
             * accueil idle) : clavier numerique de temperature par-dessus
             * ECRAN_ACCUEIL_HUB, meme schema que le scenario 13 pour le
             * homing juste au-dessus. Avant la tache 7, ce titre reutilisait
             * la constante partagee de l'ancien ecran_accueil_idle.c
             * (ECRAN_ACCUEIL_IDLE_TEMP_TITRE_BUSE, meme chaine que le vrai
             * tap sur une cellule) -- ce fichier a disparu en tache 7 et
             * ECRAN_ACCUEIL_HUB n'a NI clic sur ses cellules NI clavier de
             * consigne (voir ecran_accueil_hub.c), donc plus de bouton reel
             * a representer fidelement : litteral ci-dessous ("Nozzle
             * target", meme valeur que l'ancienne constante). Valeur initiale
             * "210" en dur (pas de tactile simule pour lire une vraie
             * consigne courante depuis ce fichier, meme limite documentee au
             * scenario 13 pour le homing) : plausible pour le scenario 10
             * dont ce cas herite l'etat (buse a 0, voir backend_factice.c). */
            clavier_ouvrir("Nozzle target", "210", CLAVIER_NUMERIQUE, demo_clavier_rappel, NULL);
        }

        /* Un cycle de pompe LVGL suffit à laisser rendre l'écran une
         * première fois avant la capture (délai nul : aucune animation
         * n'est en jeu ici, voir l'ancienne mire de la tâche 1). */
        afficheur_pomper(0);
        if (!afficheur_capturer(chemin_capture)) {
            fprintf(stderr, "echec de la capture vers %s\n", chemin_capture);
            afficheur_arreter();
            return 1;
        }
        printf("capture ecrite : %s (%dx%d)\n", chemin_capture, AFFICHEUR_LARGEUR, AFFICHEUR_HAUTEUR);
        afficheur_arreter();
        return 0;
    }

    /* Mode fenêtre : boucle jusqu'à fermeture (Ctrl+C ou fermeture de la
     * fenêtre par le gestionnaire de fenêtres). Un cycle de boucle simulée
     * par seconde écoulée, habillage_pomper() à chaque image — même ordre
     * que le brief impose pour la démonstration du simulateur. */
    uint32_t accumulateur_ms = 0;
    for (;;) {
        afficheur_pomper(16);
        habillage_pomper();
        if (app == APP_JOUET) {
            jouet_pomper(); /* voir le commentaire de tete de ce fichier */
        }
        usleep(16 * 1000);
        accumulateur_ms += 16;
        if (accumulateur_ms >= 1000) {
            cycle_simule_avec_echantillon(app);
            accumulateur_ms = 0;
        }
    }
}
