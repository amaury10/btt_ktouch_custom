/* Ecran Accueil-hub (sous-projet "graphes de temperature", tache 3) :
 * reecriture main_panel EN DEUX COLONNES -- a gauche, les lignes de chauffants
 * (nom + valeur) suivies d'un resume compact (position + outil actif,
 * vitesse/flux, mini-progression) puis d'un `lv_chart` d'historique de
 * temperature ; a droite, la grille de CINQ tuiles de menu (Homing,
 * Temperature, Actions, Configuration, Print). REMPLACE le contenu precedent
 * de ce fichier (resume une colonne + grille 5 cases sous le resume, tache 4
 * de la refonte IHM KlipperScreen -- voir l'historique git) -- le symbole
 * ECRAN_ACCUEIL_HUB, son id ("accueil_hub") et son titre ("Home") restent
 * INCHANGES (task-3-brief.md du sous-projet "graphes de temperature") :
 * app_main.c l'empile au boot, le rail lit id_accueil = ECRAN_ACCUEIL_HUB.id,
 * le chooser d'habillage (accueil_choix.h) le reference -- aucun des trois ne
 * bouge, seul le CONTENU change.
 *
 * Lignes de chauffants (tache 4 du sous-projet "graphes de temperature",
 * task-4-brief.md) : nom ("T0"/"Bed") et valeur ("actuelle/consigne") sont
 * deux `lv_label_t` DISTINCTS et adjacents, jamais un texte concatene comme
 * l'ancienne ligne de temperatures -- c'est precisement ce qui a permis a
 * cette tache de poser LV_OBJ_FLAG_CLICKABLE SEULEMENT sur le label VALEUR
 * (chauffant_valeur[i]) sans retoucher la mise en page. Taper la VALEUR ouvre
 * le clavier numerique pour editer la consigne de CE chauffant precis --
 * meme parsing/bornes que ecran_temperatures.c (reference, voir le .c).
 * Le label NOM (chauffant_nom[i]) reste EN LECTURE SEULE dans cette tache
 * (tache 5 du meme sous-projet le rendra cliquable). Regler une temperature
 * reste aussi possible via la tuile "Temperature", qui ouvre ECRAN_TEMPERATURES
 * (cible deja cliquable).
 *
 * Le graphe (`chart`) lit exclusivement klipper_temp_historique.h (tache 1 du
 * meme sous-projet) : jamais de copie du tampon circulaire entier, voir
 * ecran_accueil_hub.c pour le detail du backfill/rafraichissement.
 *
 * `ecran_accueil_hub_ctx_t` est expose ici plutot qu'opaque, meme raison que
 * les autres ecrans KlipperScreen de ce dossier : host-test/tests/
 * test_ecran_accueil_hub.c relit les libelles/couleurs/pointeurs de serie via
 * lv_label_get_text()/lv_obj_get_style_text_color()/lv_chart_get_y_array()
 * pour prouver ce que construire()/mettre_a_jour() ecrivent sans jamais
 * regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "klipper_temp_historique.h" /* KLIPPER_HISTO_SERIES */
#include "lvgl.h"

/* Nombre de lignes de chauffants visibles a gauche -- bornage delibere (le
 * detail complet, jusqu'a KLIPPER_EXTRUDEURS_MAX+1 = 9 chauffants, reste
 * derriere la tuile "Temperature" qui ouvre ECRAN_TEMPERATURES). 3 couvre
 * exactement les machines de validation reelles de ce depot (CR-10 S5 :
 * 1 extrudeur + plateau = 2 ; Snapmaker U1 : jusqu'a 4 extrudeurs, dont les
 * 2-3 premiers restent visibles ici) sans faire deborder la colonne gauche du
 * budget vertical disponible au-dessus du bandeau de notification (voir
 * ZONE_CONTENU_MAX dans ecran_accueil_hub.c). */
#define ECRAN_ACCUEIL_HUB_HEATER_LIGNES 3

/* Grille de 5 tuiles de menu, ORDRE FIXE -- table verbatim de
 * task-4-brief.md (refonte IHM KlipperScreen) : Homing -> ECRAN_HOMING,
 * Temperature -> ECRAN_TEMPERATURES, Actions -> ECRAN_ACTIONS,
 * Configuration -> ECRAN_MENU_REGLAGES, Print -> ECRAN_FICHIERS. Reutilise
 * par la boucle de construction (MENU_DEFS dans le .c) et par host-test/
 * tests/test_ecran_accueil_hub.c pour retrouver la bonne tuile -- meme
 * convention que ECRAN_MENU_REGLAGES_CASE_xxx (ecran_menu_reglages.h). */
#define ECRAN_ACCUEIL_HUB_MENU_HOMING        0
#define ECRAN_ACCUEIL_HUB_MENU_TEMPERATURE   1
#define ECRAN_ACCUEIL_HUB_MENU_ACTIONS       2
#define ECRAN_ACCUEIL_HUB_MENU_CONFIGURATION 3
#define ECRAN_ACCUEIL_HUB_MENU_PRINT         4
#define ECRAN_ACCUEIL_HUB_MENU_NB            5

/* user_data du rappel de clic d'UN label VALEUR de chauffant -- meme forme
 * que ecran_temperatures_cellule_info_t (ecran_temperatures.h), un tableau
 * PARALLELE a `chauffant_nom[]`/`chauffant_valeur[]`, indexe EXACTEMENT
 * pareil (voir ecran_accueil_hub_mettre_a_jour() dans le .c, qui remplit
 * `chauffant_infos[i]` du meme indice `total` a chaque appel). Passe
 * DIRECTEMENT comme `contexte` a clavier_ouvrir() -- meme raisonnement que
 * ecran_temperatures.h : ce pointeur suffit a identifier quel chauffant a
 * ete tape sans jamais ecrire d'etat partage avant l'ouverture. */
typedef struct {
    struct ecran_accueil_hub_ctx_s *ctx; /* jamais NULL une fois construire() passe */
    bool     est_plateau;      /* true => "heater_bed", false => extrudeurs[indice_extrudeur] */
    uint8_t  indice_extrudeur; /* valide seulement si !est_plateau */
    /* Consigne courante (C, entier), rafraichie a CHAQUE mettre_a_jour()
     * depuis etat_klipper_t::extrudeurs[i].consigne / ::plateau.consigne
     * (bornee [0, 350], meme borne que ECRAN_TEMPERATURES_TEMP_MAX -- voir
     * consigne_u16() dans le .c), jamais relue depuis l'etat backend au
     * moment du clic -- ce que le clic lit doit etre ce que le dernier rendu
     * a montre a l'utilisateur, pas un etat backend qui a pu changer
     * entre-temps. */
    uint16_t consigne_courante;
} ecran_accueil_hub_chauffant_info_t;

typedef struct ecran_accueil_hub_ctx_s {
    /* --- colonne gauche : lignes de chauffants -- `chauffant_nom[i]`/
     * `chauffant_valeur[i]` sont TOUJOURS la paire de la ligne `i`, masquee
     * (LV_OBJ_FLAG_HIDDEN) si moins de ECRAN_ACCUEIL_HUB_HEATER_LIGNES
     * chauffants sont presents. `chauffant_valeur[i]` est CLIQUABLE (tache 4,
     * voir le commentaire de tete ci-dessus) ; `chauffant_infos[i]` est le
     * tableau parallele (meme indice `i`) que son rappel de clic relit. ---- */
    lv_obj_t *chauffant_nom[ECRAN_ACCUEIL_HUB_HEATER_LIGNES];    /* "T0"/"T1"/"Bed" */
    lv_obj_t *chauffant_valeur[ECRAN_ACCUEIL_HUB_HEATER_LIGNES]; /* "205.0/210.0" */
    ecran_accueil_hub_chauffant_info_t chauffant_infos[ECRAN_ACCUEIL_HUB_HEATER_LIGNES];

    /* --- colonne gauche : resume compact, trois lignes empilees sous les
     * chauffants -- voir ecran_accueil_hub.c pour le format exact. --------- */
    lv_obj_t *position;     /* "X:.. Y:.. Z:..  T<outil_actif>" (formater_axe -- "--" si non reference) */
    lv_obj_t *vitesse_flux; /* "Speed: NN%  Flow: NN%" */
    lv_obj_t *progression;  /* "Printing: NN%" -- masque (LV_OBJ_FLAG_HIDDEN) hors impression */

    /* --- colonne gauche : graphe d'historique de temperature -- une serie
     * par chauffant PRESENT, ajoutee au plus tot des que
     * klipper_temp_historique_serie_presente(i) devient vraie (construire()
     * le fait pour les chauffants deja presents au moment de cet appel ;
     * mettre_a_jour() rattrape ceux qui apparaissent seulement plus tard --
     * cas REEL au boot, ou l'ecran est construit avant que le minuteur
     * d'echantillonnage n'ait jamais pousse le moindre point, voir
     * chart_ajouter_serie() dans ecran_accueil_hub.c). `serie[i]` indexe
     * EXACTEMENT comme klipper_temp_historique.h (i < KLIPPER_EXTRUDEURS_MAX
     * = extrudeurs[i], i == KLIPPER_EXTRUDEURS_MAX = plateau), NULL tant que
     * ce chauffant n'a jamais ete vu present. `derniere_gen` est le
     * garde-fou de rafraichissement (spec) : mettre_a_jour() ne touche au
     * chart QUE quand klipper_temp_historique_generation() a change depuis
     * cette valeur. */
    lv_obj_t          *chart;
    lv_chart_series_t *serie[KLIPPER_HISTO_SERIES];
    uint32_t            derniere_gen;

    /* --- colonne droite : cinq tuiles de menu empilees verticalement (voir
     * ECRAN_ACCUEIL_HUB_MENU_* ci-dessus pour l'indexation) : `zone_menu` est
     * le conteneur qui les porte, `menu_boutons[i]` est TOUJOURS le bouton de
     * la tuile `i`, quel que soit l'ordre de creation interne -- meme
     * convention que `rail_t.boutons[i]` (rail.h). Contenu statique : aucune
     * de ces cinq tuiles n'a besoin de relire un etat au moment du clic,
     * chacune ne fait que navigation_empiler() vers un ecran deja construit. */
    lv_obj_t *zone_menu;
    lv_obj_t *menu_boutons[ECRAN_ACCUEIL_HUB_MENU_NB];
} ecran_accueil_hub_ctx_t;

extern const ecran_desc_t ECRAN_ACCUEIL_HUB;
