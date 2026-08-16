/* Ecran Accueil-hub (sous-projet "graphes de temperature", tache 3 ; lignes de
 * chauffants affinees en boutons scrollables, tache de suivi ; gouttiere de
 * defilement + ligne de statut pleine largeur, tache de suivi -- refinement
 * 2) : reecriture main_panel EN DEUX COLONNES -- a gauche, les lignes de
 * chauffants (nom + valeur, chacune un BOUTON >= 44px de haut, voir plus bas)
 * dans un conteneur SCROLLABLE a hauteur fixe, suivies directement d'un
 * `lv_chart` d'historique de temperature qui occupe le reste de la colonne ;
 * a droite, la grille de CINQ tuiles de menu (Homing, Temperature, Actions,
 * Configuration, Print). SOUS les deux colonnes, une ligne de statut pleine
 * largeur (position + outil actif + vitesse/flux + progression, voir
 * `ecran_accueil_hub_ctx_t::statut` plus bas) -- l'ancien resume compact
 * (trois lignes empilees dans la colonne gauche, entre les chauffants et le
 * chart) est PARTI, fusionne en cette seule ligne pour agrandir le chart.
 * REMPLACE le contenu precedent
 * de ce fichier (resume une colonne + grille 5 cases sous le resume, tache 4
 * de la refonte IHM KlipperScreen -- voir l'historique git) -- le symbole
 * ECRAN_ACCUEIL_HUB, son id ("accueil_hub") et son titre ("Home") restent
 * INCHANGES (task-3-brief.md du sous-projet "graphes de temperature") :
 * app_main.c l'empile au boot, le rail lit id_accueil = ECRAN_ACCUEIL_HUB.id,
 * le chooser d'habillage (accueil_choix.h) le reference -- aucun des trois ne
 * bouge, seul le CONTENU change.
 *
 * Lignes de chauffants (taches 4 et 5 du sous-projet "graphes de
 * temperature", task-4-brief.md/task-5-brief.md, mise en boutons scrollables
 * dans une tache de suivi) : nom ("T0"/"Bed") et valeur ("actuelle/consigne")
 * sont deux `lv_button_t` DISTINCTS et adjacents (nom a gauche, valeur a
 * droite, meme ligne) -- chacun porte un UNIQUE `lv_label_t` enfant, centre
 * (meme convention que les tuiles de menu `menu_boutons[i]` plus bas : le
 * texte n'est jamais stocke a part dans le contexte, retrouve via
 * `lv_obj_get_child(bouton, 0)`, voir chauffant_bouton_label() dans le .c).
 * `lv_button_create()` pose LV_OBJ_FLAG_CLICKABLE par defaut -- c'est ce qui a
 * permis a la tache 4 de faire du bouton VALEUR (chauffant_valeur[i]) une
 * cible de clic SANS code de flag explicite, puis a la tache 5 de faire de
 * meme sur le bouton NOM (chauffant_nom[i]) pour un raccourci DIFFERENT.
 * Taper la VALEUR ouvre le clavier numerique pour editer la consigne de CE
 * chauffant precis -- meme parsing/bornes que ecran_temperatures.c (reference,
 * voir le .c). Taper le NOM bascule l'affichage de la courbe de CE chauffant
 * sur le graphe (`serie_visible[]` ci-dessous) et recolore son label -- voir
 * chauffant_nom_cb() dans le .c pour le detail, y compris la reconciliation
 * avec le grisage C3 (donnees perimees). Regler une temperature reste aussi
 * possible via la tuile "Temperature", qui ouvre ECRAN_TEMPERATURES (cible
 * deja cliquable).
 *
 * Couleur du NOM (tache de suivi de refinement) : le label NOM prend la
 * couleur de SA courbe (COULEURS_SERIE[s] dans le .c, meme mapping que
 * chauffant_info_serie_indice()) quand les donnees sont fraiches ET la courbe
 * visible, gris (COULEUR_GRISE) sinon (donnees perimees OU courbe masquee) --
 * meme regle appliquee IDENTIQUEMENT dans mettre_a_jour() (a chaque
 * rafraichissement) et chauffant_nom_cb() (immediatement au clic, voir le .c).
 * Le label VALEUR, lui, garde sa couleur commune (primaire/grise selon
 * `donnees_perimees` uniquement) -- inchange par cette regle.
 *
 * Pool scrollable (tache de suivi) : `chauffant_nom[]`/`chauffant_valeur[]`
 * sont dimensionnes pour TOUS les chauffants possibles
 * (ECRAN_ACCUEIL_HUB_HEATER_LIGNES == KLIPPER_HISTO_SERIES == 9 : 8 extrudeurs
 * + plateau), portes par `zone_chauffants`, un conteneur LV_OBJ_FLAG_SCROLLABLE
 * a HAUTEUR FIXE (assez pour ~2 lignes-boutons, voir CHAUFFANTS_ZONE_HAUTEUR
 * dans le .c) -- les chauffants PRESENTS occupent les lignes 0..total-1 de
 * facon CONTIGUE (repositionnees a chaque mettre_a_jour(), voir son
 * commentaire dans le .c), les lignes au-dela sont masquees
 * (LV_OBJ_FLAG_HIDDEN) et ne laissent AUCUN trou de defilement -- LVGL
 * n'inclut jamais un enfant LV_OBJ_FLAG_HIDDEN dans l'etendue scrollable
 * (voir lv_obj_get_scroll_bottom(), managed_components/lvgl__lvgl/src/core/
 * lv_obj_scroll.c), donc masquer suffit, aucun repositionnement des lignes
 * masquees n'est necessaire pour eviter un trou.
 *
 * Le graphe (`chart`) lit exclusivement klipper_temp_historique.h (tache 1 du
 * meme sous-projet) : jamais de copie du tampon circulaire entier, voir
 * ecran_accueil_hub.c pour le detail du backfill/rafraichissement.
 *
 * `ecran_accueil_hub_ctx_t` est expose ici plutot qu'opaque, meme raison que
 * les autres ecrans KlipperScreen de ce dossier : host-test/tests/
 * test_ecran_accueil_hub.c relit les libelles/couleurs/pointeurs de serie via
 * lv_label_get_text()/lv_obj_get_style_text_color()/lv_chart_get_y_array()
 * (sur le label ENFANT pour les lignes de chauffants, voir plus haut) pour
 * prouver ce que construire()/mettre_a_jour() ecrivent sans jamais regarder un
 * pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "klipper_temp_historique.h" /* KLIPPER_HISTO_SERIES */
#include "lvgl.h"

/* Taille du POOL de lignes de chauffants (tache de suivi de refinement) --
 * PLUS un bornage a "3 lignes visibles" (ancien comportement, historique
 * git) : chaque chauffant possible a sa ligne-bouton PROPRE dans le pool,
 * KLIPPER_HISTO_SERIES == KLIPPER_EXTRUDEURS_MAX+1 == 9 (8 extrudeurs +
 * plateau, reutilise plutot que recopie -- meme mapping FIXE que
 * klipper_temp_historique.h). Le pool entier existe TOUJOURS ; c'est
 * `zone_chauffants` (conteneur scrollable a hauteur fixe, voir plus bas) qui
 * borne combien de lignes sont visibles SANS defiler -- au-dela, l'utilisateur
 * fait defiler au lieu de perdre l'acces aux chauffants restants (l'ancien
 * bornage a 3 les rendait invisibles, accessibles seulement via la tuile
 * "Temperature"). */
#define ECRAN_ACCUEIL_HUB_HEATER_LIGNES KLIPPER_HISTO_SERIES

/* Grille de 6 tuiles de menu, ORDRE FIXE -- table verbatim de
 * task-4-brief.md (refonte IHM KlipperScreen) : Homing -> ECRAN_HOMING,
 * Temperature -> ECRAN_TEMPERATURES, Actions -> ECRAN_ACTIONS,
 * Configuration -> ECRAN_MENU_REGLAGES, Print -> ECRAN_FICHIERS, PLUS
 * USB -> ECRAN_USB (feature "Impression depuis USB", tache B -- 6e tuile
 * ajoutee APRES task-4-brief.md, en fin de liste pour ne renumeroter aucune
 * des cinq tuiles existantes ni les tests qui les referencent par nom).
 * Reutilise par la boucle de construction (MENU_DEFS dans le .c) et par
 * host-test/tests/test_ecran_accueil_hub.c pour retrouver la bonne tuile --
 * meme convention que ECRAN_MENU_REGLAGES_CASE_xxx (ecran_menu_reglages.h). */
#define ECRAN_ACCUEIL_HUB_MENU_HOMING        0
#define ECRAN_ACCUEIL_HUB_MENU_TEMPERATURE   1
#define ECRAN_ACCUEIL_HUB_MENU_ACTIONS       2
#define ECRAN_ACCUEIL_HUB_MENU_CONFIGURATION 3
#define ECRAN_ACCUEIL_HUB_MENU_PRINT         4
#define ECRAN_ACCUEIL_HUB_MENU_USB           5
#define ECRAN_ACCUEIL_HUB_MENU_NB            6

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

/* Graduations de l'echelle verticale du graphe : 4 = les bornes plus deux
 * intermediaires, ce qui tombe sur des centaines rondes avec la plage par
 * defaut 0-300 C (300/200/100/0) et laisse le graphe lisible sans le
 * surcharger de chiffres. */
#define ECRAN_ACCUEIL_HUB_ECHELLE_NB 4

typedef struct ecran_accueil_hub_ctx_s {
    /* --- colonne gauche : lignes de chauffants -- `chauffant_nom[i]`/
     * `chauffant_valeur[i]` sont TOUJOURS la paire de BOUTONS de la ligne `i`
     * (chacun un `lv_button_t` avec un unique `lv_label_t` enfant centre, voir
     * le commentaire de tete du .h), masquee (LV_OBJ_FLAG_HIDDEN) au-dela du
     * nombre de chauffants reellement PRESENTS -- les lignes presentes sont
     * repositionnees de facon CONTIGUE (0..total-1) a chaque
     * mettre_a_jour(), voir son commentaire dans le .c. `zone_chauffants` est
     * le conteneur SCROLLABLE a hauteur fixe qui les porte (LV_DIR_VER,
     * LV_SCROLLBAR_MODE_AUTO) -- le CHART/la ligne de statut/la grille de
     * menu n'en font pas partie. `chauffant_valeur[i]` (tache 4) ET
     * `chauffant_nom[i]` (tache 5) sont TOUS DEUX CLIQUABLES (lv_button_create()
     * pose LV_OBJ_FLAG_CLICKABLE par defaut) -- `chauffant_infos[i]` est le
     * tableau parallele (meme indice `i`) que les DEUX rappels de clic
     * relisent (chauffant_valeur_cb() pour la consigne, chauffant_nom_cb()
     * pour l'indice de serie du graphe, voir chauffant_info_serie_indice()
     * dans le .c). ---------------------------------------------------------- */
    lv_obj_t *zone_chauffants; /* conteneur scrollable, parent de tout le pool ci-dessous */
    lv_obj_t *chauffant_nom[ECRAN_ACCUEIL_HUB_HEATER_LIGNES];    /* bouton "T0"/"T1"/"Bed" */
    lv_obj_t *chauffant_valeur[ECRAN_ACCUEIL_HUB_HEATER_LIGNES]; /* bouton "205.0/210.0" */
    ecran_accueil_hub_chauffant_info_t chauffant_infos[ECRAN_ACCUEIL_HUB_HEATER_LIGNES];

    /* --- ligne de statut pleine largeur, SOUS les deux colonnes (tache de
     * suivi, refinement 2) -- remplace les trois lignes de resume qui
     * vivaient auparavant empilees dans la colonne gauche, entre les
     * chauffants et le chart (historique git). Format exact (voir
     * mettre_a_jour() dans le .c) : "X:.. Y:.. Z:..  T<outil_actif>
     * Speed: NN%  Flow: NN%" ("--" par axe non reference, formater_axe()),
     * suivi de "  Printing: NN%" SEULEMENT si `impression_en_cours` -- jamais
     * un widget masque/demasque comme l'ancienne ligne `progression`, la
     * sous-chaine est simplement absente/presente selon le cas (une SEULE
     * ligne visuelle en TOUTE circonstance, jamais un flag LV_OBJ_FLAG_HIDDEN
     * sur `statut` lui-meme). LECTURE SEULE (aucun lv_obj_add_event_cb
     * dessus) -- c'est ce qui la rend exemptee de l'assertion "au-dessus du
     * bandeau de notification" que respectent le chart et les tuiles (voir
     * STATUT_Y dans le .c pour le detail complet du compromis). */
    lv_obj_t *statut;

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
    /* Echelle verticale du graphe (demande utilisateur : « afficher
       l'echelle verticale de temperature dans le graph ») : ECHELLE_NB
       libelles fixes, du HAUT (CHART_Y_MAX) vers le BAS (CHART_Y_MIN),
       alignes sur les lignes de division. Exposes pour que host-test lise
       les valeurs affichees plutot que de deviner. */
    lv_obj_t *echelle[ECRAN_ACCUEIL_HUB_ECHELLE_NB];
    lv_obj_t          *chart;
    lv_chart_series_t *serie[KLIPPER_HISTO_SERIES];
    uint32_t            derniere_gen;
    /* Raccourci NOM (tache 5, task-5-brief.md) : `serie_visible[s]` --
     * indexee EXACTEMENT comme `serie[]` ci-dessus, PAS comme les lignes de
     * chauffants (`i`) -- suit UNIQUEMENT l'intention utilisateur (dernier
     * clic sur le NOM du chauffant de cette serie), TOUT A `true` a la
     * construction (chart_ajouter_serie() n'appelle jamais
     * lv_chart_hide_series(), une serie neuve est donc deja visible par
     * defaut cote LVGL -- ce tableau ne fait que suivre/refleter cet etat,
     * jamais le contraire). `donnees_perimees` memorise le DERNIER argument
     * `donnees_perimees` recu par mettre_a_jour() -- necessaire pour que
     * chauffant_nom_cb() (le .c) puisse reconcilier le grisage C3 (donnees
     * perimees) avec ce toggle SANS attendre le prochain mettre_a_jour() :
     * la couleur du label NOM doit rester grise si perime OU masque, celle de
     * SA courbe (COULEURS_SERIE[s]) seulement si frais ET visible -- voir le
     * commentaire complet dans le .c (chauffant_nom_cb() et la boucle de
     * grisage de mettre_a_jour()). */
    bool serie_visible[KLIPPER_HISTO_SERIES];
    bool donnees_perimees;

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
