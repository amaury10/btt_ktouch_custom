#include "backend_factice.h"

#include <stdio.h>
#include <string.h>

#include "journal.h"
#include "klipper_fichiers.h"

/* Étiquette de journalisation : convention reprise du reste du firmware
 * (voir app_main.c, rescue.c), pour que /log reste lisible par module. */
static const char *TAG = "backend_factice";

/* Scénario courant, choisi par backend_factice_scenario(). Un seul backend
 * factice tourne à la fois dans le socle : un compteur statique suffit, et
 * évite au descripteur de porter un état qui n'a pas sa place dans
 * etat_klipper_t. */
static int g_scenario = 0;

/* Tâche 9 : bascule de backend_factice_commande_echoue(), voir son
 * commentaire dans backend_factice.h. Statique de fichier comme g_scenario
 * ci-dessus, pour la même raison. */
static bool g_commande_echoue = false;

/* Progression du scénario 1, portée ICI plutôt que relue depuis `etat` : le
 * socle remet `etat` à zéro avant CHAQUE appel de rafraichir() (voir le
 * contrat documenté sur backend_desc_t::rafraichir dans backend.h), donc
 * `e->progression` y vaut toujours 0 en entrée — un fait qui n'était pas
 * documenté avant la revue de fin de jalon 2a, et que ce fichier lisait à
 * l'envers (CRITICAL 1) : la progression restait figée à
 * FACTICE_PAS_PROGRESSION pour toujours, etat_store_valider() ne détectait
 * plus aucun changement après le premier cycle, et generation cessait
 * d'avancer. Un compteur statique de fichier, comme g_scenario juste
 * au-dessus, est la façon correcte de porter un état d'un appel à l'autre. */
static float g_progression_scenario1 = 0.0f;

/* Durée totale supposée du scénario 1, choisie arbitrairement mais fixe :
 * elle permet de recalculer temps_restant_s de façon cohérente avec la
 * progression, sans jamais tirer de nombre aléatoire. */
#define FACTICE_DUREE_IMPRESSION_S 3600u

/* Pas d'avancement de la progression à chaque rafraîchissement du scénario 1.
 * Une valeur ronde, assez grande pour qu'un test la voie bouger en deux
 * appels, assez petite pour laisser un simulateur regarder une impression
 * progresser sur plusieurs secondes. */
#define FACTICE_PAS_PROGRESSION 0.01f

/* Outil actif du scénario 11 (« U1 »), porté ICI entre deux appels pour la
 * même raison que g_progression_scenario1 ci-dessus : le socle remet `etat`
 * à zéro avant chaque rafraîchissement, donc `outil_actif` y vaut toujours 0
 * en entrée. Un changeur d'outils réel fait tourner son outil actif d'un
 * cycle à l'autre ; ce compteur statique modélise exactement ça, sans jamais
 * tirer de nombre aléatoire (même politique que g_progression_scenario1). */
static uint8_t g_outil_actif_u1 = 0;

/* « Respiration » thermique du scenario 10 (« CR-10 »), meme mecanique de
 * compteur statique que g_outil_actif_u1 ci-dessus et sans plus de tirage
 * aleatoire. Une imprimante reelle au repos ne rend JAMAIS deux fois la meme
 * temperature au dixieme pres : son thermistor oscille. Ce scenario, lui,
 * rendait un etat rigoureusement identique a chaque cycle -- or
 * etat_store_valider() (core/etat_store.c) compare au memcmp et n'incremente
 * PAS sa generation quand rien n'a change, si bien qu'aucun ecran n'etait
 * jamais rafraichi. C'est ce qui a fait decouvrir le trou de
 * generation_externe_klipper() (app_main.c) ; le corriger ne suffit pas a
 * rendre ce scenario fidele, d'ou cette oscillation. */
static uint8_t g_respiration_cr10 = 0;
static const float FACTICE_CR10_RESPIRATION[4] = { 0.0f, 0.2f, 0.1f, -0.1f };

/* Nombre d'extrudeurs modélisés par le scénario 11 (« U1 ») : un changeur
 * d'outils à quatre têtes. Nommé plutôt que répété en dur, pour que la
 * boucle qui remplit `extrudeurs[]` et le modulo qui fait tourner
 * `outil_actif` restent visiblement le même nombre. */
#define FACTICE_U1_NB_EXTRUDEURS 4u

/* Macros du scénario 10 (« CR-10 ») : une imprimante mono-extrudeur
 * d'entrée de gamme, quatre macros simples, aucune particularité. */
static const char *const g_macros_cr10[] = {
    "BED_MESH_CALIBRATE",
    "LOAD_FILAMENT",
    "UNLOAD_FILAMENT",
    "LIGHTS_TOGGLE",
};

/* Macros du scénario 11 (« U1 ») : huit macros dont une préfixée "_"
 * (convention Klipper pour une macro "cachée" — présente dans l'état, c'est
 * à l'interface de la filtrer plus tard, pas à ce backend), une qui
 * démontrera des paramètres (PURGE_PARAM, tâche 6) et une sentinelle de
 * démonstration d'échec (MACRO_ECHEC, voir backend_factice_commande()
 * ci-dessous — échoue TOUJOURS, quel que soit le scénario actif). */
static const char *const g_macros_u1[] = {
    "HOME_ALL",
    "_CACHEE",
    "PURGE_PARAM",
    "MACRO_ECHEC",
    "CHANGE_TOOL",
    "LOAD_FILAMENT",
    "UNLOAD_FILAMENT",
    "CALIBRATE_OFFSETS",
};

/* Écrit le nom de la `indice_un`-ième macro (1-indexé, lisible humainement :
 * la première s'appelle "MACRO_01", pas "MACRO_00") du scénario 12
 * (« 8 têtes »). Factorisée pour que backend_factice_rafraichir() (qui les
 * range dans `etat.macros[]`) et macro_connue() ci-dessous (qui valide un
 * nom reçu par commande()) ne puissent jamais diverger sur le format. */
static void factice_nom_macro_8tetes(uint8_t indice_un, char *sortie, size_t taille)
{
    snprintf(sortie, taille, "MACRO_%02u", (unsigned)indice_un);
}

/* Tache 2, jalon "browser de fichiers" : fichiers gcode factices, pour que le
 * simulateur et le futur ecran (tache 3) aient une liste a afficher.
 * Contrairement a g_macros_cr10/g_macros_u1 ci-dessus (specifiques a un
 * scenario), ces fichiers sont peuples INCONDITIONNELLEMENT dans le preambule
 * de backend_factice_rafraichir() (comme nb_extrudeurs=1/plateau.presente
 * juste au-dessus) : le navigateur de fichiers n'a pas de raison de varier
 * selon la machine simulee, contrairement aux macros qui illustrent des
 * paliers materiels differents. "calibration/cube.gcode" exerce en plus le
 * cas d'un chemin avec sous-dossier (voir rpc_lire_fichiers, moonraker_rpc.h,
 * qui accepte des '/' dans `.path`). */
static const char *const g_fichiers_factices[] = {
    "benchy.gcode",
    "calibration/cube.gcode",
    "CE3_test.gcode",
};

/* Publie les fichiers factices dans le store dedie (klipper_fichiers.h) UNE
 * fois au demarrage : la liste ne vit plus dans etat_klipper_t (sortie du
 * champ `fichiers[]` pour liberer la RAM interne -- voir klipper_fichiers.h),
 * ce n'est donc plus un champ de `etat` que rafraichir() remplirait, mais un
 * store process-wide. Aucun scenario ne fait varier cette liste (le navigateur
 * de fichiers ne depend pas de la machine simulee, contrairement aux macros). */
static void factice_publier_fichiers(void)
{
    klipper_fichiers_t f;
    memset(&f, 0, sizeof(f));
    f.nb = (uint8_t)(sizeof(g_fichiers_factices) / sizeof(g_fichiers_factices[0]));
    for (uint8_t i = 0; i < f.nb; i++) {
        snprintf(f.noms[i], KLIPPER_FICHIER_MAX, "%s", g_fichiers_factices[i]);
    }
    f.tronques = false;
    klipper_fichiers_definir(&f);
}

/* Rend vrai si `nom` est une macro que CE backend connaît, tous scénarios
 * confondus (10, 11, 12 : les scénarios 0-9 n'annoncent aucune macro). Ne
 * dépend PAS du scénario actuellement sélectionné par
 * backend_factice_scenario() : une commande peut arriver longtemps après le
 * dernier rafraîchissement qui a produit la liste, et backend_desc_t::commande
 * ne garantit pas de relire un `etat` fiable (voir son commentaire dans
 * backend.h) -- interroger la connaissance STATIQUE du backend plutôt qu'un
 * état de passage est le choix le plus robuste. MACRO_ECHEC est un nom connu
 * (présente dans g_macros_u1) mais traitée à part par l'appelant AVANT ce
 * test, pour rendre ESP_FAIL plutôt que ESP_OK. */
static bool macro_connue(const char *nom)
{
    for (size_t i = 0; i < sizeof(g_macros_cr10) / sizeof(g_macros_cr10[0]); i++) {
        if (strcmp(nom, g_macros_cr10[i]) == 0) {
            return true;
        }
    }
    for (size_t i = 0; i < sizeof(g_macros_u1) / sizeof(g_macros_u1[0]); i++) {
        if (strcmp(nom, g_macros_u1[i]) == 0) {
            return true;
        }
    }
    for (uint8_t i = 1; i <= KLIPPER_MACROS_MAX; i++) {
        char candidat[KLIPPER_MACRO_NOM_MAX];
        factice_nom_macro_8tetes(i, candidat, sizeof(candidat));
        if (strcmp(nom, candidat) == 0) {
            return true;
        }
    }
    return false;
}

/* Extraction JSON délibérément bornée (strstr/strchr), PAS cJSON : ce
 * backend est un jouet SYNTHÉTIQUE dont l'entrée `arguments_json` vient
 * toujours du firmware lui-même (core/boucle.c, jamais d'un tiers non
 * fiable) -- l'analyse d'un JSON quelconque (imbrication, échappement,
 * unicode) est le travail du vrai backend Moonraker
 * (apps/klipper/backend_moonraker.c, tâche 3/6), seul consommateur de
 * réponses HTTP externes. Ajouter cJSON ici pour un unique champ plat
 * `{"nom":"<macro>"}` alourdirait core/ (qui n'en dépend nulle part
 * ailleurs -- voir le commentaire de web_macros.h sur le soin apporté à
 * garder core/ testable au harnais hôte sans dépendance lourde) pour un
 * bénéfice nul ici. Rend faux si `arguments_json` est NULL, mal formé, ou si
 * le nom extrait ne tient pas dans `taille_sortie`. */
static bool factice_extraire_nom_macro(const char *arguments_json, char *sortie, size_t taille_sortie)
{
    if (arguments_json == NULL || sortie == NULL || taille_sortie == 0) {
        return false;
    }
    const char *cle = strstr(arguments_json, "\"nom\"");
    if (cle == NULL) {
        return false;
    }
    const char *deux_points = strchr(cle, ':');
    if (deux_points == NULL) {
        return false;
    }
    const char *guillemet_ouvrant = strchr(deux_points, '"');
    if (guillemet_ouvrant == NULL) {
        return false;
    }
    guillemet_ouvrant++;
    const char *guillemet_fermant = strchr(guillemet_ouvrant, '"');
    if (guillemet_fermant == NULL) {
        return false;
    }
    size_t longueur = (size_t)(guillemet_fermant - guillemet_ouvrant);
    if (longueur == 0 || longueur >= taille_sortie) {
        return false;
    }
    memcpy(sortie, guillemet_ouvrant, longueur);
    sortie[longueur] = '\0';
    return true;
}

void backend_factice_scenario(int numero)
{
    g_scenario = numero;
}

void backend_factice_commande_echoue(bool echoue)
{
    g_commande_echoue = echoue;
}

void backend_factice_reinit(void)
{
    /* Remet à zéro l'état inter-appels PROCESSUS de ce backend (revue finale
     * jalon 3a) : g_progression_scenario1 et g_outil_actif_u1 sont
     * fichier-statiques et s'accumulent d'une exécution de scénario à l'autre.
     * Sur cible ça n'a aucune importance (un seul démarrage) ; dans le
     * harnais hôte, une suite qui exerce le scénario 1 assez de fois avant
     * test_boucle_cycle.c peut faire boucler la progression au-delà de 1.0 et
     * rendre un test dépendant de l'ordre. Une suite qui veut un point de
     * départ propre appelle ceci d'abord. */
    g_progression_scenario1 = 0.0f;
    g_outil_actif_u1 = 0;
    g_respiration_cr10 = 0;
}

static esp_err_t backend_factice_demarrer(void *etat, const backend_hote_t *hote)
{
    etat_klipper_t *e = (etat_klipper_t *)etat;
    /* Remise à zéro défensive : le brief ne l'exige pas (le socle est censé
     * fournir un état déjà nul), mais un backend qui ne suppose rien de l'état
     * qu'on lui tend est moins susceptible de propager un débris d'une session
     * précédente si un futur appelant oublie de le faire. Ce n'est pas un
     * oubli du contrat « demarrer ne fait que journaliser » : c'est un ajout
     * volontaire, au-delà du minimum. */
    memset(e, 0, sizeof(*e));
    /* Tache 2, jalon "browser de fichiers" : les fichiers factices vivent
     * desormais dans le store dedie (klipper_fichiers.h), pas dans `etat` --
     * peuples UNE fois ici, au demarrage. */
    factice_publier_fichiers();
    JOURNAL_INFO(TAG, "demarrage (hote=%s port=%u)",
                 hote != NULL ? hote->adresse : "?",
                 hote != NULL ? (unsigned)hote->port : 0u);
    return ESP_OK;
}

static esp_err_t backend_factice_rafraichir(void *etat)
{
    etat_klipper_t *e = (etat_klipper_t *)etat;
    etat_klipper_t nouveau;
    memset(&nouveau, 0, sizeof(nouveau));

    /* Valeur par défaut : une machine mono-extrudeur avec plateau chauffant,
     * pour les scenarios 0-4 ci-dessous (y compris au repos, ou rien ne
     * chauffe mais le materiel existe toujours) -- ceux du 2b, inchanges par
     * la tache 2 du jalon 3a. Migration v2 (tache 1, jalon 3a) : valait
     * implicitement avant que le seul extrudeur/plateau de la structure ne
     * PUISSE etre que celui-la ; desormais explicite puisque etat_klipper_t
     * peut representer 0 a 8 extrudeurs. Les scenarios paliers 10/11/12
     * (tache 2) modelisent des machines differentes et ECRASENT ces valeurs
     * par defaut dans leur propre `case` ci-dessous. */
    nouveau.nb_extrudeurs = 1;
    nouveau.extrudeurs[0].presente = true;
    nouveau.plateau.presente = true;
    nouveau.outil_actif = 0;

    /* Tache 2, jalon "browser de fichiers" : les fichiers factices ne sont
     * PLUS un champ de `etat` -- ils vivent dans le store dedie
     * (klipper_fichiers.h), peuple une fois par backend_factice_demarrer()
     * (voir factice_publier_fichiers()). Rien a faire ici. */

    switch (g_scenario) {
    case 0:
        /* Repos : rien n'imprime, rien ne chauffe. */
        snprintf(nouveau.etat, sizeof(nouveau.etat), "standby");
        break;

    case 1: {
        /* Impression en cours : la progression avance à partir de sa valeur
         * précédente, portée par g_progression_scenario1 (fichier-statique,
         * voir sa déclaration plus haut) — jamais relue depuis `etat`, que le
         * socle remet à zéro avant cet appel. Pas de compteur caché "de
         * plus" au sens propre du terme : c'est le seul état que porte ce
         * scénario, et surtout pas de rand(). Au-delà de 1.0 elle reboucle à
         * 0, comme une nouvelle impression qui démarrerait. */
        g_progression_scenario1 += FACTICE_PAS_PROGRESSION;
        if (g_progression_scenario1 > 1.0f) {
            g_progression_scenario1 = 0.0f;
        }
        float progression = g_progression_scenario1;

        snprintf(nouveau.etat, sizeof(nouveau.etat), "printing");
        nouveau.extrudeurs[0].actuelle = 210.0f;
        nouveau.extrudeurs[0].consigne = 210.0f;
        nouveau.plateau.actuelle = 60.0f;
        nouveau.plateau.consigne = 60.0f;
        snprintf(nouveau.fichier, sizeof(nouveau.fichier), "piece_test.gcode");
        nouveau.progression = progression;
        nouveau.temps_restant_s =
            (uint32_t)((1.0f - progression) * (float)FACTICE_DUREE_IMPRESSION_S + 0.5f);
        nouveau.impression_en_cours = true;
        nouveau.impression_en_pause = false;
        break;
    }

    case 2:
        /* Pause : l'impression existe toujours mais n'avance plus. */
        snprintf(nouveau.etat, sizeof(nouveau.etat), "paused");
        nouveau.extrudeurs[0].actuelle = 210.0f;
        nouveau.extrudeurs[0].consigne = 210.0f;
        nouveau.plateau.actuelle = 60.0f;
        nouveau.plateau.consigne = 60.0f;
        snprintf(nouveau.fichier, sizeof(nouveau.fichier), "piece_test.gcode");
        nouveau.progression = 0.5f;
        nouveau.temps_restant_s = FACTICE_DUREE_IMPRESSION_S / 2u;
        nouveau.impression_en_cours = true;
        nouveau.impression_en_pause = true;
        break;

    case 4:
        /* Valeurs aberrantes : sonde de buse largement hors plage plausible
         * (voir TEMPERATURE_MIN_C/MAX_C dans ui/widgets/tuile.c, [-5, 500])
         * et plateau négatif comme le ferait une sonde déconnectée -- ne
         * vérifie pas la même chose que le scénario 3 (fichier au maximum de
         * sa capacité, mais températures restées PLAUSIBLES à 350°C).
         * Ce scénario-ci exerce ui_format_temperature() côté "--", pas côté
         * débordement d'affichage : un écran qui afficherait 999.0 ou
         * -999.0 comme s'il s'agissait d'une vraie mesure reproduirait
         * exactement le défaut que ce champ existe pour empêcher (même
         * politique que le grisage sur donnees_perimees, voir ecran.h). */
        snprintf(nouveau.etat, sizeof(nouveau.etat), "printing");
        snprintf(nouveau.fichier, sizeof(nouveau.fichier), "piece_test.gcode");
        nouveau.extrudeurs[0].actuelle = 999.0f;
        nouveau.extrudeurs[0].consigne = 210.0f;
        nouveau.plateau.actuelle = -999.0f;
        nouveau.plateau.consigne = 60.0f;
        nouveau.progression = 0.42f;
        nouveau.temps_restant_s = 1800u;
        nouveau.impression_en_cours = true;
        nouveau.impression_en_pause = false;
        break;

    case 13:
        /* Tache 5, jalon 3b (fond mis a jour tache 7, retrait de l'ancien
         * accueil idle) : arriere-plan de --scenario 13 (simulateur/
         * main.c), qui ouvre en plus la confirmation de homing par-dessus --
         * IDENTIQUE au scenario 10 juste en-dessous (empile ici, pas
         * duplique) : au repos (ECRAN_ACCUEIL_HUB, jamais ECRAN_ACCUEIL,
         * contrairement au repli "scenario 3" que suivent 5/6/7/8/9 -- une
         * confirmation de HOMING n'a de sens que sur l'accueil au repos) ET
         * tous les axes deja references (necessaire pour que Home X declenche
         * reellement la confirmation, spec tache 5 §7 -- un axe NON reference
         * enverrait le gcode direct, sans dialogue, ce que cette capture ne
         * veut pas montrer). */
    case 14:
        /* Tache 6, jalon 3b (fond mis a jour tache 7, retrait de l'ancien
         * accueil idle) : arriere-plan de --scenario 14 (simulateur/
         * main.c), qui ouvre en plus le clavier numerique de temperature
         * par-dessus -- IDENTIQUE au scenario 10/13 (empile ici, pas
         * duplique) : au repos, sur ECRAN_ACCUEIL_HUB. Les axes references
         * ne jouent aucun role pour cette capture (le clavier de temperature
         * n'en depend pas, contrairement au homing du scenario 13) mais
         * heriter du meme etat que 10/13 documente mieux l'intention "meme
         * arriere-plan que les autres captures au repos" qu'un etat distinct
         * n'apporterait rien. */
    case 10:
        /* Palier « CR-10 » (tache 2, jalon 3a) : mono-extrudeur + plateau
         * chauffant deja etablis par le preambule ci-dessus, rien a changer
         * la-dessus. Au repos, quatre macros simples -- aucune particularite,
         * c'est le scenario le plus proche des scenarios 0-4 du 2b, juste
         * avec des macros non vides. */
        snprintf(nouveau.etat, sizeof(nouveau.etat), "standby");
        /* Temperatures ambiantes, pas le 0.0 que laissait le preambule : une
         * imprimante au repos lit la piece ou elle se trouve, jamais le zero
         * absolu. Constate en produisant les captures du README -- l'accueil
         * affichait "0.0/0.0", fidele au backend mais faux sur une machine
         * reelle, ce qui donnait a l'ecran l'air en panne. Meme valeur
         * ambiante que le scenario 11 ci-dessous, pour que les deux paliers
         * racontent la meme piece. Aucune assertion de
         * host-test/tests/test_backend_factice.c ne porte sur ces champs pour
         * ce scenario ; les scenarios 13/14, qui se declarent "IDENTIQUES au
         * 10", en heritent -- c'est voulu. */
        nouveau.extrudeurs[0].actuelle = 24.0f + FACTICE_CR10_RESPIRATION[g_respiration_cr10 % 4u];
        nouveau.extrudeurs[0].consigne = 0.0f;
        nouveau.plateau.actuelle = 23.0f + FACTICE_CR10_RESPIRATION[(g_respiration_cr10 + 2u) % 4u];
        nouveau.plateau.consigne = 0.0f;
        g_respiration_cr10 = (uint8_t)((g_respiration_cr10 + 1u) % 4u);
        nouveau.nb_macros = sizeof(g_macros_cr10) / sizeof(g_macros_cr10[0]);
        for (uint8_t i = 0; i < nouveau.nb_macros; i++) {
            snprintf(nouveau.macros[i], KLIPPER_MACRO_NOM_MAX, "%s", g_macros_cr10[i]);
        }
        nouveau.macros_tronquees = false;
        /* Tache 4, jalon 3b : machine entierement referencee (homee) --
         * necessaire pour que la capture --scenario 10 (le scenario que le
         * brief de la tache 4 nomme explicitement) montre le pad de jog
         * ENTIEREMENT actif plutot que grise par defaut (axes_references
         * valait 0 avant ce changement, ce qui aurait grise les six boutons
         * de jog des le premier cycle -- rien avant la tache 4 ne lisait ce
         * champ pour ce scenario, donc rien ne l'avait jamais reclame). */
        nouveau.axes_references = 0x1u | 0x2u | 0x4u;
        nouveau.position[0] = 120.0f;
        nouveau.position[1] = 100.0f;
        nouveau.position[2] = 5.0f;
        /* Limites machine, retraction firmware et input shaper : des valeurs
         * de CR-10 plausibles. Ces champs existent deja dans etat_klipper_t
         * (aucune croissance de la structure -- contrainte dure, voir son
         * en-tete et l'historique des debordements de pile), mais aucun
         * scenario du backend factice ne les avait jamais peuples : les ecrans
         * Limits / Retraction / Input Shaper n'affichaient donc que des "-" et
         * "(not set)", ce qui les rendait impossibles a montrer en
         * fonctionnement (constate en produisant les captures du README). */
        nouveau.limite_velocity = 300.0f;
        nouveau.limite_accel = 3000.0f;
        nouveau.limite_square_corner = 5.0f;
        nouveau.limite_accel_to_decel = 1500.0f;
        nouveau.retr_length = 6.5f;
        nouveau.retr_speed = 45.0f;
        /* Non nul volontairement : ce champ traite 0 comme "absent/non recu"
         * (voir etat_klipper.h), la ligne resterait donc a "-". */
        nouveau.retr_unretract_extra = 0.2f;
        nouveau.retr_unretract_speed = 40.0f;
        snprintf(nouveau.shaper_type_x, sizeof(nouveau.shaper_type_x), "mzv");
        snprintf(nouveau.shaper_type_y, sizeof(nouveau.shaper_type_y), "ei");
        nouveau.shaper_freq_x = 41.4f;
        nouveau.shaper_freq_y = 36.2f;
        break;

    case 11: {
        /* Palier « U1 » (tache 2, jalon 3a) : changeur d'outils a quatre
         * extrudeurs. Trois tetes a temperature ambiante (froides, comme un
         * outil qui n'a pas ete utilise depuis un moment) et une chaude
         * (l'outil qui vient de deposer du filament) -- "temperatures idle
         * realistes, une chaude" du brief. */
        nouveau.nb_extrudeurs = FACTICE_U1_NB_EXTRUDEURS;
        for (uint8_t i = 0; i < FACTICE_U1_NB_EXTRUDEURS; i++) {
            nouveau.extrudeurs[i].presente = true;
            nouveau.extrudeurs[i].actuelle = 24.0f;
            nouveau.extrudeurs[i].consigne = 0.0f;
        }
        nouveau.extrudeurs[2].actuelle = 205.0f;
        nouveau.extrudeurs[2].consigne = 210.0f;

        /* outil_actif tourne d'un cycle a l'autre (voir g_outil_actif_u1
         * ci-dessus) : c'est CE cycle-ci qui publie la valeur PRECEDENTE
         * avant d'avancer le compteur pour le prochain rafraichissement. */
        nouveau.outil_actif = g_outil_actif_u1;
        g_outil_actif_u1 = (uint8_t)((g_outil_actif_u1 + 1u) % FACTICE_U1_NB_EXTRUDEURS);

        nouveau.plateau.presente = true;
        nouveau.plateau.actuelle = 60.0f;
        nouveau.plateau.consigne = 60.0f;

        snprintf(nouveau.etat, sizeof(nouveau.etat), "standby");
        nouveau.nb_macros = sizeof(g_macros_u1) / sizeof(g_macros_u1[0]);
        for (uint8_t i = 0; i < nouveau.nb_macros; i++) {
            snprintf(nouveau.macros[i], KLIPPER_MACRO_NOM_MAX, "%s", g_macros_u1[i]);
        }
        nouveau.macros_tronquees = false;
        break;
    }

    case 12:
        /* Palier « 8 tetes » (tache 2, jalon 3a) : huit extrudeurs
         * synthetiques, tous presents, temperatures ambiantes -- ce
         * scenario sert a exercer l'echelle (affichage, boucles), pas une
         * combinaison de temperatures particuliere. */
        nouveau.nb_extrudeurs = KLIPPER_EXTRUDEURS_MAX;
        for (uint8_t i = 0; i < KLIPPER_EXTRUDEURS_MAX; i++) {
            nouveau.extrudeurs[i].presente = true;
            nouveau.extrudeurs[i].actuelle = 24.0f;
            nouveau.extrudeurs[i].consigne = 0.0f;
        }
        nouveau.outil_actif = 0;
        nouveau.plateau.presente = true;
        nouveau.plateau.actuelle = 60.0f;
        nouveau.plateau.consigne = 60.0f;

        snprintf(nouveau.etat, sizeof(nouveau.etat), "standby");

        /* 48 macros exactement -- KLIPPER_MACROS_MAX, la borne reelle de la
         * structure (jamais un nombre en dur different de la constante :
         * exactement le piege que web_nb_macros_serialisables() existe pour
         * absorber cote consommateur, voir firmware/main/web_macros.h, mais
         * qui n'excuse pas ce producteur-ci d'ecrire hors tableau). */
        nouveau.nb_macros = KLIPPER_MACROS_MAX;
        for (uint8_t i = 0; i < KLIPPER_MACROS_MAX; i++) {
            factice_nom_macro_8tetes((uint8_t)(i + 1u), nouveau.macros[i], KLIPPER_MACRO_NOM_MAX);
        }
        /* Le producteur reel (synthetique ici, mais c'est le meme champ que
         * remplirait un analyseur Moonraker face a une config qui declare
         * plus de macros que KLIPPER_MACROS_MAX) en connait davantage que ce
         * qu'il vient d'annoncer : le signaler honnetement plutot que de
         * pretendre qu'il n'y en a que 48. Vrai UNIQUEMENT sur ce scenario. */
        nouveau.macros_tronquees = true;
        break;

    case 3:
    default:
        /* Valeurs extrêmes mais PLAUSIBLES : nom de fichier au maximum de sa
         * capacité et température de buse haute-mais-crédible, pour
         * vérifier qu'un affichage ne déborde nulle part sans pour autant
         * déclencher le rendu "--" du scénario 4 ci-dessus. Sert aussi de
         * scénario par défaut pour tout numéro inconnu — mieux vaut une
         * valeur voyante qu'un comportement silencieusement indéfini. */
        snprintf(nouveau.etat, sizeof(nouveau.etat), "printing");
        memset(nouveau.fichier, 'x', KLIPPER_FICHIER_MAX - 1);
        nouveau.fichier[KLIPPER_FICHIER_MAX - 1] = '\0';
        nouveau.extrudeurs[0].actuelle = 350.0f;
        nouveau.extrudeurs[0].consigne = 350.0f;
        nouveau.plateau.actuelle = 150.0f;
        nouveau.plateau.consigne = 150.0f;
        nouveau.progression = 0.99f;
        nouveau.temps_restant_s = KLIPPER_TEMPS_RESTANT_MAX_S;
        nouveau.impression_en_cours = true;
        nouveau.impression_en_pause = false;
        break;
    }

    *e = nouveau;
    return ESP_OK;
}

static void backend_factice_arreter(void *etat)
{
    (void)etat;
    JOURNAL_INFO(TAG, "arret");
}

static esp_err_t backend_factice_commande(void *etat, const char *action,
                                           const char *arguments_json)
{
    (void)etat;

    if (strcmp(action, BACKEND_ACTION_PAUSE) == 0 ||
        strcmp(action, BACKEND_ACTION_REPRENDRE) == 0 ||
        strcmp(action, BACKEND_ACTION_ANNULER) == 0 ||
        strcmp(action, BACKEND_ACTION_URGENCE) == 0) {
        if (g_commande_echoue) {
            /* Echec deliberement force (voir backend_factice_commande_echoue()) :
             * une action par ailleurs valide echoue quand meme, pour exercer le
             * chemin d'echec ASYNCHRONE (commande acceptee en file, executee plus
             * tard, et C'EST LA qu'elle echoue) plutot que le seul chemin
             * synchrone (file pleine) que ui_commander() peut deja signaler
             * directement a l'appelant. */
            JOURNAL_ALERTE(TAG, "commande %s en echec (force par backend_factice_commande_echoue)", action);
            return ESP_FAIL;
        }
        JOURNAL_INFO(TAG, "commande %s", action);
        return ESP_OK;
    }

    if (strcmp(action, BACKEND_ACTION_MACRO) == 0) {
        /* Tache 2, jalon 3a. `nom` extrait de arguments_json (voir
         * factice_extraire_nom_macro() : bornee, sans cJSON, justifie plus
         * haut). MACRO_ECHEC est une sentinelle de demonstration -- elle
         * echoue TOUJOURS, quel que soit le scenario actif, pour exercer le
         * chemin d'echec d'une commande de macro cote interface (tache 6). */
        char nom[KLIPPER_MACRO_NOM_MAX];
        if (!factice_extraire_nom_macro(arguments_json, nom, sizeof(nom))) {
            JOURNAL_ALERTE(TAG, "commande macro sans nom exploitable dans arguments_json");
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (strcmp(nom, "MACRO_ECHEC") == 0) {
            JOURNAL_ALERTE(TAG, "commande macro %s en echec (sentinelle de demonstration)", nom);
            return ESP_FAIL;
        }
        if (macro_connue(nom)) {
            JOURNAL_INFO(TAG, "commande macro %s", nom);
            return ESP_OK;
        }
        JOURNAL_ALERTE(TAG, "commande macro inconnue %s", nom);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (strcmp(action, BACKEND_ACTION_GCODE) == 0) {
        /* Tache 1, jalon 3b. Le script est deja construit par l'appelant
         * (klipper_gcode.c) -- ce backend factice n'a rien a en extraire, il
         * accepte simplement pour que le simulateur puisse "executer" un jog
         * sans erreur. Un vrai jog changerait la position (etat_klipper_t),
         * mais le factice n'a pas a la simuler pour ce jalon (aucun ecran ne
         * lit encore de position courante) -- voir le rapport de la tache 1
         * si un futur jalon en a besoin. */
        if (arguments_json == NULL) {
            JOURNAL_ALERTE(TAG, "commande gcode sans arguments_json");
            return ESP_ERR_NOT_SUPPORTED;
        }
        JOURNAL_INFO(TAG, "commande gcode %s", arguments_json);
        return ESP_OK;
    }

    /* Une action inconnue doit échouer fort et explicitement, pour que
     * l'interface puisse griser un bouton en connaissant la raison — jamais
     * l'ignorer en silence. */
    JOURNAL_ALERTE(TAG, "commande inconnue %s", action);
    return ESP_ERR_NOT_SUPPORTED;
}

static const backend_desc_t g_backend_factice_desc = {
    .nom = "factice",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = backend_factice_demarrer,
    .rafraichir = backend_factice_rafraichir,
    .arreter = backend_factice_arreter,
    .commande = backend_factice_commande,
};

const backend_desc_t *backend_factice_desc(void)
{
    return &g_backend_factice_desc;
}
