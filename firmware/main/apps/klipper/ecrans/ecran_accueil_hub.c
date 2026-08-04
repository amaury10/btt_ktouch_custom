/* Implementation : voir ecran_accueil_hub.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation a droite du rail
 * persistant, sous la barre d'etat construite par habillage.c) : DEUX
 * colonnes cote a cote. La colonne GAUCHE porte les lignes de chauffants
 * (nom + valeur, ECRAN_ACCUEIL_HUB_HEATER_LIGNES au plus) puis DIRECTEMENT un
 * `lv_chart` d'historique de temperature qui occupe le reste de la colonne.
 * La colonne DROITE porte les six tuiles de menu (Homing/Temperature/Actions/
 * Configuration/Print/USB -- USB ajoutee par la feature "Impression depuis
 * USB", tache B), empilees VERTICALEMENT
 * (contre une rangee horizontale sous le resume dans l'ancienne mise en page
 * une colonne) -- geometrie verifiee par _Static_assert, meme discipline que
 * ecran_menu_reglages.c/ecran_deplacer.c. SOUS les deux colonnes, une ligne
 * de statut UNIQUE pleine largeur (`ctx->statut`, position + outil actif +
 * vitesse/flux + progression) -- tache de suivi (refinement 2) : remplace
 * l'ancien resume compact TROIS lignes qui vivait empile dans la colonne
 * gauche entre les chauffants et le chart, ce qui agrandit le chart d'autant
 * (voir CHART_Y/STATUT_Y plus bas). Meme tache de suivi : `zone_chauffants`
 * reserve desormais une petite gouttiere a droite du bouton VALEUR pour que
 * sa barre de defilement (LV_PART_SCROLLBAR) ne recouvre plus son bord droit
 * (voir CHAUFFANT_GOUTTIERE_SCROLLBAR plus bas).
 *
 * ECART delibere par rapport a l'ancien contenu de ce fichier (resume quatre
 * lignes pleine largeur + grille 5 cases EN DESSOUS) : le sous-projet
 * "graphes de temperature" (task-3-brief.md) demande une refonte en deux
 * colonnes pour loger le graphe d'historique (klipper_temp_historique.h,
 * tache 1 du meme sous-projet) sans repousser la grille de menu hors de
 * l'ecran. Toutes les constantes de geometrie sont DERIVEES (jamais
 * recopiees) et verifiees les unes par rapport aux autres, meme idiome que
 * ecran_deplacer.c.
 *
 * Lignes de chauffants (taches 4 et 5, mise en boutons scrollables dans une
 * tache de suivi) : voir le commentaire de tete du .h pour pourquoi nom/
 * valeur sont deux `lv_button_t` distincts (chacun un `lv_label_t` enfant
 * unique et centre) plutot qu'un texte concatene -- `lv_button_create()` pose
 * LV_OBJ_FLAG_CLICKABLE par defaut sur `chauffant_valeur[i]` ET
 * `chauffant_nom[i]`, sans code de flag explicite. Taper la valeur ouvre le
 * clavier numerique (chauffant_valeur_cb() plus bas) pour editer la consigne
 * de CE chauffant -- meme parsing/bornes/gcode que cellule_bouton_cb()/
 * cellule_clavier_rappel() de ecran_temperatures.c (copie plutot que
 * partagee, meme choix que le reste de ce depot vis-a-vis de
 * construire_arguments_gcode()/envoyer_gcode() plus bas). Taper le nom
 * (chauffant_nom_cb() plus bas) bascule l'affichage de la courbe de CE
 * chauffant sur le graphe et recolore son label (couleur de SA courbe si
 * frais et visible, gris sinon) -- raccourci INDEPENDANT de celui de la
 * valeur, meme ligne mais deux gestes distincts. Le pool de lignes
 * (ECRAN_ACCUEIL_HUB_HEATER_LIGNES == KLIPPER_HISTO_SERIES) couvre TOUS les
 * chauffants possibles ; `zone_chauffants` (conteneur scrollable a hauteur
 * fixe) montre les premieres lignes sans defiler, le reste accessible par
 * glissement vertical -- voir CHAUFFANTS_ZONE_HAUTEUR plus bas.
 *
 * Libelles de menu SANS accent ("Configuration", pas de caractere accentue)
 * -- aucun texte affiche a l'ecran dans ce depot n'utilise de caractere
 * accentue (voir "Nozzle target"/"Bed target"/rail.c "Accueil"/"Home"/
 * "Macros"/"STOP") -- les polices Montserrat embarquees (sdkconfig.defaults,
 * CONFIG_LV_FONT_MONTSERRAT_*) ne sont jamais garanties couvrir le Latin-1
 * Supplement, un glyphe manquant se rendrait en tofu silencieux. */
#include "ecran_accueil_hub.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "clavier.h"
#include "ecran_actions.h"       /* ECRAN_ACTIONS */
#include "ecran_fichiers.h"      /* ECRAN_FICHIERS */
#include "ecran_homing.h"        /* ECRAN_HOMING */
#include "ecran_menu_reglages.h" /* ECRAN_MENU_REGLAGES */
#include "ecran_usb.h"           /* ECRAN_USB -- feature "Impression depuis USB", tache B */
/* ECRAN_TEMPERATURES + ECRAN_TEMPERATURES_TITRE_BUSE/PLATEAU/TEMP_MIN/MAX :
 * reutilises TELS QUELS pour le clavier de consigne pose par cette tache --
 * memes titres/bornes que le panneau dedie, jamais une seconde source de
 * verite qui pourrait diverger (voir chauffant_valeur_cb()/chauffant_clavier_rappel()
 * plus bas). */
#include "ecran_temperatures.h"
#include "habillage.h" /* habillage_notifier() */
#include "klipper_gcode.h"
#include "klipper_temp_historique.h"
#include "navigation.h" /* navigation_empiler() */
#include "source_etat.h" /* ui_commander() */
#include "tuile.h"      /* ui_format_temperature() */

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
/* Pas de HAUTEUR_CONTENU ici (contrairement a l'ancien contenu de ce
 * fichier) : ZONE_CONTENU_MAX plus bas (derivee de BANDEAU_Y_ECRAN, plus
 * stricte) est le SEUL plafond vertical dont ce fichier a besoin -- le
 * conserver en plus aurait ete une constante jamais lue. */

#define MARGE    14
#define COL_ECART 14 /* ecart horizontal entre les deux colonnes */

/* --- Deux colonnes qui remplissent exactement la largeur du contenu
 * (_Static_assert plus bas) -- 350px chacune, le meme partage a peu pres
 * egal (~360px) que demande task-3-brief.md. -------------------------- */
#define GAUCHE_X       MARGE
#define GAUCHE_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - COL_ECART) / 2)
#define DROITE_X       (GAUCHE_X + GAUCHE_LARGEUR + COL_ECART)
#define DROITE_LARGEUR (LARGEUR_CONTENU - MARGE - DROITE_X)

/* Meme convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_menu_reglages.c (voir son commentaire complet) :
 * bande couverte par le bandeau de notification de habillage.c, en
 * coordonnees ABSOLUES d'ecran. ZONE_CONTENU_MAX (DERIVEE) est le plafond
 * relatif au contenu (jamais recopie) sous lequel les DEUX colonnes doivent
 * rester -- task-3-brief.md : "the bottom of each column stays above the
 * banner". */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN     (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)
#define ZONE_CONTENU_MAX    (BANDEAU_Y_ECRAN - BARRE_HAUTEUR_ECRAN)

#define CONTENU_Y 6 /* Y de depart commun aux deux colonnes */

/* --- Colonne gauche : lignes de chauffants -------------------------------
 * Nom ("T0"/"Bed", bouton etroit) + valeur ("205.0/210.0", bouton qui prend
 * le reste de la colonne) sur la MEME ligne, deux lv_button_t distincts et
 * adjacents (voir le commentaire de tete du .h) -- chacun >= 44px de haut
 * (cible tactile, _Static_assert plus bas), porte par `zone_chauffants`
 * (conteneur SCROLLABLE a hauteur FIXE, CHAUFFANTS_ZONE_HAUTEUR, juste assez
 * pour CHAUFFANT_LIGNES_VISIBLES lignes) -- le pool complet
 * (ECRAN_ACCUEIL_HUB_HEATER_LIGNES == KLIPPER_HISTO_SERIES == 9) existe
 * TOUJOURS, seules les lignes AU-DELA de CHAUFFANT_LIGNES_VISIBLES exigent un
 * defilement pour devenir visibles -- jamais l'inverse (le chart, lui, ne doit
 * JAMAIS retrecir pour leur faire de la place, voir CHART_HAUTEUR plus bas et
 * son _Static_assert >= 180). --------------------------------------------- */
#define CHAUFFANT_LIGNE_HAUTEUR   44 /* >= 44px, _Static_assert plus bas */
#define CHAUFFANT_LIGNE_ECART      8
#define CHAUFFANT_NOM_LARGEUR     70
#define CHAUFFANT_VALEUR_X      (CHAUFFANT_NOM_LARGEUR + 8)
/* Gouttiere de defilement (tache de suivi, refinement 2) : `zone_chauffants`
 * garde LV_PART_SCROLLBAR opaque et large de 4px (voir construire() plus
 * bas), dessinee au ras du bord DROIT du conteneur -- sans cette gouttiere,
 * le bouton VALEUR remplissait exactement la largeur du conteneur
 * (CHAUFFANT_VALEUR_X + CHAUFFANT_VALEUR_LARGEUR == GAUCHE_LARGEUR) et la
 * barre venait recouvrir ses 4 derniers pixels. 8px (marge visuelle en plus
 * des 4px de la barre elle-meme) plutot que d'ajouter un padding au
 * conteneur -- reduire la largeur du bouton VALEUR est la modification la
 * plus locale, aucun autre enfant de `zone_chauffants` n'a besoin d'etre
 * touche. */
#define CHAUFFANT_GOUTTIERE_SCROLLBAR 8
#define CHAUFFANT_VALEUR_LARGEUR (GAUCHE_LARGEUR - CHAUFFANT_VALEUR_X - CHAUFFANT_GOUTTIERE_SCROLLBAR)

/* Nombre de lignes-boutons visibles SANS defiler dans `zone_chauffants` --
 * DELIBEREMENT 2 (et non 3, malgre "~2-3 button rows" de la spec) : c'est ce
 * qui laisse CHART_HAUTEUR >= 180px (_Static_assert plus bas) au budget
 * vertical disponible (ZONE_CONTENU_MAX) -- le chart garde la priorite sur le
 * nombre de lignes visibles sans defiler, voir le commentaire de tete de ce
 * bloc. Au-dela de CHAUFFANT_LIGNES_VISIBLES chauffants presents, le
 * conteneur defile (LV_SCROLLBAR_MODE_AUTO, voir construire() plus bas). */
#define CHAUFFANT_LIGNES_VISIBLES 2

#define CHAUFFANTS_ZONE_Y      CONTENU_Y
#define CHAUFFANTS_ZONE_HAUTEUR (CHAUFFANT_LIGNES_VISIBLES * CHAUFFANT_LIGNE_HAUTEUR + \
                                  (CHAUFFANT_LIGNES_VISIBLES - 1) * CHAUFFANT_LIGNE_ECART)

/* --- Colonne gauche : graphe -- juste SOUS les lignes de chauffants (tache
 * de suivi, refinement 2 : plus de resume compact entre les deux, voir
 * STATUT_Y plus bas pour ou il est parti) -- occupe tout le reste de la
 * colonne jusqu'au plafond ZONE_CONTENU_MAX (DERIVEE, jamais un nombre
 * choisi a la main) -- c'est ce qui rend le graphe "aussi grand que
 * possible" sans jamais chevaucher le bandeau de notification. ---------- */
#define ZONE_ECART   8 /* ecart vertical entre les "blocs" de la colonne gauche */
#define CHART_Y       (CHAUFFANTS_ZONE_Y + CHAUFFANTS_ZONE_HAUTEUR + ZONE_ECART)
#define CHART_HAUTEUR (ZONE_CONTENU_MAX - CHART_Y)

/* --- Ligne de statut pleine largeur, SOUS les deux colonnes (tache de
 * suivi, refinement 2) -- position + outil actif + vitesse/flux +
 * progression, voir mettre_a_jour() pour le format exact et
 * ecran_accueil_hub_ctx_t::statut (le .h) pour le detail complet. Spec de ce
 * refinement : la ligne doit couvrir LARGEUR_CONTENU - 2*MARGE, positionnee
 * au ras du bas des DEUX colonnes -- STATUT_Y >= ZONE_CONTENU_MAX (le bas
 * commun du chart ET des tuiles, voir les deux _Static_assert plus bas qui
 * le garantissent) place donc cette ligne SOUS les deux colonnes, jamais un
 * chevauchement visuel avec elles. Centree dans la bande du bandeau de
 * notification (BANDEAU_HAUTEUR_ECRAN de haut, qui commence PRECISEMENT a
 * ZONE_CONTENU_MAX) plutot que collee a son sommet -- purement esthetique,
 * cette ligne EST AUTORISEE a y entrer (contrairement au chart/aux tuiles)
 * car elle est LECTURE SEULE
 * (jamais de lv_obj_add_event_cb dessus) : une notification qui la recouvre
 * temporairement est un compromis deja accepte ailleurs dans ce depot pour
 * du contenu non interactif (meme raisonnement que la rangee d'etat
 * d'impression de l'ancien hub, historique git). ------------------------- */
#define STATUT_HAUTEUR 20
#define STATUT_Y       (ZONE_CONTENU_MAX + (BANDEAU_HAUTEUR_ECRAN - STATUT_HAUTEUR) / 2)

/* --- Colonne droite : six tuiles empilees verticalement (feature
 * "Impression depuis USB", tache B : USB ajoutee en 6e case, voir
 * ECRAN_ACCUEIL_HUB_MENU_USB dans le .h), meme hauteur fixe -- TUILE_HAUTEUR
 * est DERIVEE pour que les six tuiles + les cinq ecarts remplissent
 * EXACTEMENT la meme plage verticale que la colonne gauche (CONTENU_Y ..
 * ZONE_CONTENU_MAX), _Static_assert plus bas. TUILE_ECART reduit de 10 a 8
 * (par rapport a l'ancienne grille de 5 tuiles) : DROITE_ZONE_HAUTEUR (370)
 * ne se divise pas exactement par 6 tuiles + 5 ecarts de 10 (l'egalite EXACTE
 * exigee par le _Static_assert plus bas echouerait), alors qu'elle le fait
 * avec des ecarts de 8 (330 / 6 = 55 pile) -- purement arithmetique, aucune
 * autre raison de ce choix. ------------------------------------------------ */
#define TUILE_ECART            8
#define DROITE_ZONE_HAUTEUR    (ZONE_CONTENU_MAX - CONTENU_Y)
#define TUILE_HAUTEUR           ((DROITE_ZONE_HAUTEUR - (ECRAN_ACCUEIL_HUB_MENU_NB - 1) * TUILE_ECART) / \
                                  ECRAN_ACCUEIL_HUB_MENU_NB)

_Static_assert(MARGE + GAUCHE_LARGEUR + COL_ECART + DROITE_LARGEUR + MARGE == LARGEUR_CONTENU,
                "les deux colonnes ne remplissent plus exactement la largeur du contenu");
_Static_assert(TUILE_HAUTEUR >= 44, "les tuiles de menu doivent rester >= 44px de cible tactile");
_Static_assert(ECRAN_ACCUEIL_HUB_MENU_NB * TUILE_HAUTEUR + (ECRAN_ACCUEIL_HUB_MENU_NB - 1) * TUILE_ECART ==
                    DROITE_ZONE_HAUTEUR,
                "les six tuiles ne remplissent plus exactement la hauteur disponible de la colonne droite");
_Static_assert(CHAUFFANT_LIGNE_HAUTEUR >= 44,
                "les boutons NOM/VALEUR des lignes de chauffants doivent rester >= 44px de cible tactile");
_Static_assert(CHAUFFANT_VALEUR_X + CHAUFFANT_VALEUR_LARGEUR + CHAUFFANT_GOUTTIERE_SCROLLBAR == GAUCHE_LARGEUR,
                "les boutons NOM+VALEUR + la gouttiere de defilement ne remplissent plus exactement la colonne gauche");
_Static_assert(CHAUFFANT_VALEUR_LARGEUR >= 100,
                "le bouton VALEUR d'une ligne de chauffant est trop etroit pour afficher \"actuelle/consigne\"");
/* CHART_HAUTEUR >= 180 (task-3-brief.md/spec de ce refinement : "usable
 * height, >= ~180px") -- zone_chauffants (hauteur FIXE, voir plus haut) ne
 * doit jamais grossir avec le nombre de chauffants presents pour lui faire de
 * la place : au-dela de CHAUFFANT_LIGNES_VISIBLES chauffants, on defile, on
 * ne retrecit jamais le chart. */
_Static_assert(CHART_HAUTEUR >= 180, "le graphe descend sous la hauteur utilisable minimale (180px) dans la colonne gauche");
/* Meme garde-fou que la grille de menu de ecran_menu_reglages.c (voir son
 * commentaire complet) : le bas de CHAQUE colonne, en coordonnees ABSOLUES
 * d'ecran, doit rester au-dessus du bandeau de notification -- sans quoi une
 * notification recouvrirait le graphe ET bloquerait le tap sur les tuiles. */
_Static_assert(BARRE_HAUTEUR_ECRAN + CHART_Y + CHART_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la colonne gauche (graphe) chevauche la bande du bandeau de notification de l'habillage");
_Static_assert(BARRE_HAUTEUR_ECRAN + CONTENU_Y + DROITE_ZONE_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la colonne droite (tuiles) chevauche la bande du bandeau de notification de l'habillage");
/* La ligne de statut, elle, est DELIBEREMENT EXEMPTEE des deux garde-fous
 * ci-dessus (voir le commentaire complet de STATUT_Y plus haut : LECTURE
 * SEULE, jamais une cible de clic) -- ces deux assertions verifient
 * seulement qu'elle reste (a) au ras ou sous le bas commun des deux colonnes
 * (jamais un chevauchement visuel PAR-DESSUS le chart/les tuiles) et (b)
 * entierement a l'interieur de l'ecran (jamais coupee en bas). */
_Static_assert(STATUT_Y >= ZONE_CONTENU_MAX,
                "la ligne de statut chevauche le bas du chart/des tuiles");
_Static_assert(BARRE_HAUTEUR_ECRAN + STATUT_Y + STATUT_HAUTEUR <= HAUTEUR_ECRAN_TOTALE,
                "la ligne de statut deborde du bas de l'ecran");

#define COULEUR_FOND             0x10161D
#define COULEUR_FOND_CHART       0x1B2430
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Une couleur distincte par serie du graphe (task-3-brief.md : "distinct
 * color per series"), indexee EXACTEMENT comme klipper_temp_historique.h
 * (0..KLIPPER_EXTRUDEURS_MAX-1 = extrudeurs, KLIPPER_EXTRUDEURS_MAX =
 * plateau) -- le plateau recoit une teinte froide (bleu ardoise) qui ne
 * ressemble a aucune couleur d'extrudeur, meme si un pire cas a 8 extrudeurs
 * finit par reutiliser un ton proche (aucune palette de 9 couleurs n'est
 * parfaitement distinguable a l'oeil, hors de portee de cette tache). */
static const uint32_t COULEURS_SERIE[KLIPPER_HISTO_SERIES] = {
    0xEF4444, /* T0 rouge */
    0xF59E0B, /* T1 ambre */
    0xEAB308, /* T2 jaune */
    0x22C55E, /* T3 vert */
    0x14B8A6, /* T4 sarcelle */
    0x3B82F6, /* T5 bleu */
    0x8B5CF6, /* T6 violet */
    0xEC4899, /* T7 rose */
    0x94A3B8, /* Bed (indice KLIPPER_EXTRUDEURS_MAX) : bleu ardoise */
};

/* Bornes Y du graphe (task-3-brief.md : "sensible Y range, e.g. 0..300") --
 * couvre la plage realiste d'une buse (jusqu'a ~300 C) et d'un plateau (bien
 * en dessous), sans avoir besoin de s'adapter dynamiquement aux valeurs
 * courantes -- un axe qui bouge rendrait la courbe plus dure a lire d'un
 * coup d'oeil que quelques pixels "perdus" en bas du graphe. */
#define CHART_Y_MIN 0
#define CHART_Y_MAX 300

/* Cree la serie `i` sur le chart (couleur COULEURS_SERIE[i]) et la backfille
 * depuis le store -- factorisee entre construire() ET mettre_a_jour() : au
 * BOOT REEL (app_main.c), navigation_empiler(&ECRAN_ACCUEIL_HUB) tourne
 * AVANT que le minuteur d'echantillonnage (echantillon_temp_cb(), periode
 * 5 s) n'ait jamais pousse le moindre point -- klipper_temp_historique_serie_presente()
 * rend donc FALSE pour toutes les series au moment de construire(), et aucune
 * n'y est ajoutee. Sans ce rattrapage cote mettre_a_jour() (plus bas), le
 * chart resterait vide A JAMAIS : son propre garde-fou de generation
 * n'ajoute jamais de point a une serie qui n'existe pas encore. Verifie
 * empiriquement par capture --scenario 11 (U1, chauffants non nuls) pendant
 * l'implementation de cette tache -- le graphe restait plat sans ce
 * correctif. `tampon` LOCAL, jamais une copie du store entier -- meme regle
 * que le backfill de construire(). */
static void chart_ajouter_serie(ecran_accueil_hub_ctx_t *ctx, uint8_t i)
{
    ctx->serie[i] = lv_chart_add_series(ctx->chart, lv_color_hex(COULEURS_SERIE[i]), LV_CHART_AXIS_PRIMARY_Y);
    if (ctx->serie[i] == NULL) {
        return; /* ne devrait jamais arriver hors epuisement memoire LVGL */
    }
    int16_t tampon[KLIPPER_HISTO_POINTS];
    size_t n = klipper_temp_historique_serie(i, tampon, KLIPPER_HISTO_POINTS);
    for (size_t k = 0; k < n; k++) {
        lv_chart_set_next_value(ctx->chart, ctx->serie[i], tampon[k]);
    }
    /* Applique l'etat de visibilite stocke AU MOMENT DE LA CREATION : une serie
     * peut naitre APRES que l'utilisateur a masque son chauffant (toggle sur le
     * nom pendant la fenetre de boot ou serie[i]==NULL). Sans ceci, la courbe
     * naitrait visible alors que le nom est grise -- le nom promet justement (voir
     * chauffant_nom_cb()) que le masquage "s'appliquera des que la serie existe".
     * A construire() tous les serie_visible[] valent true -> hide(false) = no-op. */
    lv_chart_hide_series(ctx->chart, ctx->serie[i], !ctx->serie_visible[i]);
}

/* Ecrit "%.1f" si `reference` est vrai, "--" sinon -- copie de formater_axe()
 * de ecran_deplacer.c : ne jamais presenter comme mesuree une position
 * qu'aucun homing n'a etablie. Copie plutot que partagee, meme choix que le
 * reste de ce depot (voir le commentaire de tete de ecran_zcalibrate.c pour
 * le meme choix ailleurs). */
static void formater_axe(char *sortie, size_t taille, float valeur, bool reference)
{
    if (!reference) {
        snprintf(sortie, taille, "--");
        return;
    }
    snprintf(sortie, taille, "%.1f", (double)valeur);
}

/* ------------------------------------------------------------------------
 * Raccourci accueil (tache 4, task-4-brief.md) : taper la VALEUR d'une ligne
 * de chauffant ouvre le clavier numerique pour editer sa consigne. Copie des
 * memes idiomes que ecran_temperatures.c (construire_arguments_gcode()/
 * envoyer_gcode()/nom_chauffeur_extrudeur()/consigne_u16()) -- copie plutot
 * que partagee, meme choix que le reste de ce depot (voir formater_axe()
 * ci-dessus pour le meme choix ailleurs dans ce fichier). Les TITRES et les
 * BORNES, eux, sont REUTILISES directement depuis ecran_temperatures.h
 * (ECRAN_TEMPERATURES_TITRE_BUSE/PLATEAU, ECRAN_TEMPERATURES_TEMP_MIN/MAX) --
 * jamais une seconde constante qui pourrait diverger du panneau dedie.
 * ------------------------------------------------------------------------ */

/* Tampon suffisant pour {"script":"<gcode>"} : KLIPPER_GCODE_MAX (160,
 * klipper_gcode.h) plus la marge du wrapper JSON (guillemets, cle "script",
 * accolades) -- meme raisonnement que GCODE_ARGS_MAX dans ecran_temperatures.c. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Construit {"script":"<script>"} via cJSON (jamais un snprintf a la main) :
 * copie de construire_arguments_gcode() de ecran_temperatures.c -- un vrai
 * octet 0x0A brut dans une chaine JSON violerait RFC 8259. Rend false SANS
 * TOUCHER `sortie` si un argument est invalide, si cJSON echoue (memoire), ou
 * si le resultat ne tient pas dans `taille`. */
static bool construire_arguments_gcode(const char *script, char *sortie, size_t taille)
{
    if (script == NULL || sortie == NULL || taille == 0) {
        return false;
    }
    cJSON *racine = cJSON_CreateObject();
    if (racine == NULL) {
        return false;
    }
    if (cJSON_AddStringToObject(racine, "script", script) == NULL) {
        cJSON_Delete(racine);
        return false;
    }
    char *texte = cJSON_PrintUnformatted(racine);
    cJSON_Delete(racine);
    if (texte == NULL) {
        return false;
    }
    size_t longueur = strlen(texte);
    bool ok = longueur < taille;
    if (ok) {
        memcpy(sortie, texte, longueur + 1);
    }
    cJSON_free(texte);
    return ok;
}

static void envoyer_gcode(const char *script)
{
    if (script == NULL) {
        return;
    }
    char arguments[GCODE_ARGS_MAX];
    if (!construire_arguments_gcode(script, arguments, sizeof(arguments))) {
        return; /* ne devrait jamais arriver : GCODE_ARGS_MAX suffit toujours */
    }
    ui_commander(BACKEND_ACTION_GCODE, arguments);
}

/* Nom du chauffeur Klipper d'un extrudeur : indice 0 => "extruder", indice N
 * (1..7) => "extruderN" -- PAS "extruder0" pour l'indice 0. Copie de
 * nom_chauffeur_extrudeur() de ecran_temperatures.c. Rend faux SANS toucher
 * `sortie` si le tampon est trop court ou si `indice` deborde
 * KLIPPER_EXTRUDEURS_MAX. */
static bool nom_chauffeur_extrudeur(uint8_t indice, char *sortie, size_t taille)
{
    if (sortie == NULL || taille == 0 || indice >= KLIPPER_EXTRUDEURS_MAX) {
        return false;
    }
    int ecrit = (indice == 0) ? snprintf(sortie, taille, "extruder")
                               : snprintf(sortie, taille, "extruder%u", (unsigned)indice);
    return ecrit >= 0 && (size_t)ecrit < taille;
}

/* Convertit une consigne backend (float, potentiellement NaN/negative/hors
 * plage) en entier [0, ECRAN_TEMPERATURES_TEMP_MAX] affichable/envoyable --
 * copie de consigne_u16() de ecran_temperatures.c, meme borne (reutilisee,
 * jamais recopiee en dur). isnan() teste EXPLICITEMENT en premier -- comparer
 * un NaN a 0.0f rend faux dans les DEUX sens, ce qui laisserait tomber dans
 * la conversion (uint16_t)v finale avec un NaN : comportement indefini en C
 * (6.3.1.4), jamais accepte silencieusement ici. */
static uint16_t consigne_u16(float valeur)
{
    if (isnan(valeur) || valeur <= 0.0f) {
        return 0;
    }
    if (valeur > (float)ECRAN_TEMPERATURES_TEMP_MAX) {
        return (uint16_t)ECRAN_TEMPERATURES_TEMP_MAX;
    }
    return (uint16_t)valeur;
}

/* Rend `info->ctx` -> nom du chauffeur Klipper de CE chauffant (extrudeur ou
 * "heater_bed"), dans `sortie`. Copie de cellule_info_nom_chauffeur() de
 * ecran_temperatures.c -- factorise entre chauffant_valeur_cb() (pour le
 * titre "Nozzle"/"Bed") et chauffant_clavier_rappel() (pour le gcode
 * reellement envoye) : les deux ne doivent jamais diverger sur QUEL
 * chauffeur une ligne precise designe. */
static bool chauffant_info_nom_chauffeur(const ecran_accueil_hub_chauffant_info_t *info, char *sortie, size_t taille)
{
    if (info->est_plateau) {
        int ecrit = snprintf(sortie, taille, "heater_bed");
        return ecrit >= 0 && (size_t)ecrit < taille;
    }
    return nom_chauffeur_extrudeur(info->indice_extrudeur, sortie, taille);
}

/* Rend l'indice de serie du graphe (0..KLIPPER_HISTO_SERIES-1, meme mapping
 * FIXE que klipper_temp_historique.h/ctx->serie[]) que designe `info` --
 * factorise entre chart_ajouter_serie()/construire()/mettre_a_jour() (deja
 * indexes par `i` de serie directement) et chauffant_nom_cb() plus bas (qui,
 * lui, ne connait `info` que via `chauffant_infos[]`, indexe par NUMERO DE
 * LIGNE, pas par numero de serie -- les deux NE coincident PAS forcement pour
 * un extrudeur : mettre_a_jour() COMPACTE les lignes presentes (voir son
 * commentaire plus bas), donc la ligne `total` d'un extrudeur `i` absent
 * avant lui vaut total < i -- meme si ECRAN_ACCUEIL_HUB_HEATER_LIGNES ==
 * KLIPPER_HISTO_SERIES, le pool entier ne garantit PAS que ligne == serie). */
static uint8_t chauffant_info_serie_indice(const ecran_accueil_hub_chauffant_info_t *info)
{
    return info->est_plateau ? (uint8_t)KLIPPER_EXTRUDEURS_MAX : info->indice_extrudeur;
}

/* Retrouve le `lv_label_t` enfant d'UN bouton de ligne de chauffant (NOM ou
 * VALEUR, voir chauffant_bouton_creer() plus bas) -- meme convention que
 * `menu_boutons[i]` (le texte n'est jamais stocke a part dans le contexte,
 * toujours le premier et unique enfant du bouton). Factorise entre
 * mettre_a_jour() (texte + couleur) et chauffant_nom_cb() (couleur seule, au
 * clic) : les deux doivent recolorer EXACTEMENT le meme label, jamais le
 * bouton lui-meme (qui n'a pas de style de texte propre). */
static lv_obj_t *chauffant_bouton_label(lv_obj_t *bouton)
{
    return lv_obj_get_child(bouton, 0);
}

/* Rappel du clavier numerique ouvert par chauffant_valeur_cb() ci-dessous --
 * `contexte` EST directement `&ctx->chauffant_infos[i]`. `valeur == NULL`
 * (annule) : rien (spec). Sinon parse en entier, borne
 * [ECRAN_TEMPERATURES_TEMP_MIN, ECRAN_TEMPERATURES_TEMP_MAX] -- MEMES bornes
 * EXACTES que cellule_clavier_rappel() de ecran_temperatures.c, une saisie
 * hors borne OU non numerique => notification d'erreur, AUCUN gcode envoye. */
static void chauffant_clavier_rappel(const char *valeur, void *contexte)
{
    ecran_accueil_hub_chauffant_info_t *info = contexte;
    if (valeur == NULL || info == NULL) {
        return;
    }

    /* strtol() plutot que sscanf("%d") : rend un pointeur de fin qu'on peut
     * comparer au terme de la chaine, la seule facon fiable de distinguer
     * "210" (valide) de "210abc"/"12.5"/""/"  " (non numerique). Chaine vide
     * (clavier valide sans rien saisir) : `fin == valeur`, rejetee
     * explicitement ci-dessous. */
    char *fin = NULL;
    errno = 0;
    long cible = strtol(valeur, &fin, 10);
    bool numerique = (fin != valeur) && (fin != NULL) && (*fin == '\0') && (errno == 0);
    bool dans_bornes = numerique && cible >= ECRAN_TEMPERATURES_TEMP_MIN && cible <= ECRAN_TEMPERATURES_TEMP_MAX;

    if (!dans_bornes) {
        habillage_notifier("Invalid temperature (0-350)", true);
        return;
    }

    char chauffeur[40];
    if (!chauffant_info_nom_chauffeur(info, chauffeur, sizeof(chauffeur))) {
        return; /* ne devrait jamais arriver : indice/tampon toujours valides depuis ce clavier */
    }
    char script[KLIPPER_GCODE_MAX];
    if (!klipper_gcode_consigne_temp(script, sizeof(script), chauffeur, (uint16_t)cible)) {
        return; /* ne devrait jamais arriver : chauffeur/cible deja valides ci-dessus */
    }
    envoyer_gcode(script);
}

/* Tap sur le label VALEUR d'une ligne de chauffant. `info` est passe
 * DIRECTEMENT comme `contexte` a clavier_ouvrir() -- meme raisonnement que
 * cellule_bouton_cb() de ecran_temperatures.c : aucun etat "en attente" a
 * poser avant l'ouverture, clavier_ouvrir() est deja lui-meme un singleton
 * qui refuse une seconde ouverture (voir clavier.h). */
static void chauffant_valeur_cb(lv_event_t *e)
{
    ecran_accueil_hub_chauffant_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }

    const char *titre = info->est_plateau ? ECRAN_TEMPERATURES_TITRE_PLATEAU : ECRAN_TEMPERATURES_TITRE_BUSE;
    char valeur_initiale[8];
    snprintf(valeur_initiale, sizeof(valeur_initiale), "%u", (unsigned)info->consigne_courante);
    clavier_ouvrir(titre, valeur_initiale, CLAVIER_NUMERIQUE, chauffant_clavier_rappel, info);
}

/* ------------------------------------------------------------------------
 * Raccourci accueil (tache 5, task-5-brief.md) : taper le NOM d'une ligne de
 * chauffant bascule l'affichage de sa courbe sur le graphe et grise/degrise
 * le label -- RIEN a voir avec le raccourci VALEUR ci-dessus (pas de clavier,
 * pas de gcode), un geste purement local a l'ecran.
 * ------------------------------------------------------------------------ */

/* Tap sur le bouton NOM d'une ligne de chauffant. `info` est passe
 * DIRECTEMENT comme `contexte` (meme tableau `chauffant_infos[]` que
 * chauffant_valeur_cb() ci-dessus, un SECOND lv_obj_add_event_cb() sur le
 * MEME element du tableau -- voir la boucle de construire() plus bas) --
 * `lv_event_get_target(e)` (et non `info`) donne le BOUTON clique, dont
 * chauffant_bouton_label() retrouve le label enfant a recolorer.
 *
 * Regle de reconciliation avec le grisage C3 (donnees_perimees, voir la
 * boucle de mettre_a_jour() plus bas) : la couleur du label NOM doit
 * refleter LES DEUX etats a la fois --
 *   grise                    si (donnees_perimees OU courbe masquee)
 *   couleur de SA courbe     seulement si (fraiches ET courbe visible)
 * (COULEURS_SERIE[s], le meme tableau que celui qui colore la courbe sur le
 * chart -- le nom promet visuellement "c'est CETTE courbe que je bascule")
 * -- jamais l'un qui efface la memoire de l'autre. Cette fonction applique
 * la regle IMMEDIATEMENT (sans attendre le prochain mettre_a_jour()) en
 * relisant `ctx->donnees_perimees`, la valeur du DERNIER appel a
 * mettre_a_jour() (voir son commentaire de tete dans le .h) ; mettre_a_jour()
 * applique la MEME regle a chaque rafraichissement en relisant
 * `ctx->serie_visible[]`, l'etat que CE rappel vient de faire evoluer -- les
 * deux cotes de la reconciliation partagent donc les deux memes variables,
 * jamais une troisieme source de verite qui pourrait diverger. */
static void chauffant_nom_cb(lv_event_t *e)
{
    ecran_accueil_hub_chauffant_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }
    ecran_accueil_hub_ctx_t *ctx = info->ctx;

    uint8_t s = chauffant_info_serie_indice(info);
    ctx->serie_visible[s] = !ctx->serie_visible[s];
    if (ctx->serie[s] != NULL) {
        /* NULL possible : ce chauffant n'a encore jamais ete vu present par
         * le store (voir chart_ajouter_serie()) -- le toggle reste memorise
         * dans serie_visible[] et s'appliquera au chart des que
         * mettre_a_jour() creera la serie (rattrapage deja existant, tache 3). */
        lv_chart_hide_series(ctx->chart, ctx->serie[s], !ctx->serie_visible[s]);
    }

    uint32_t couleur = (ctx->donnees_perimees || !ctx->serie_visible[s]) ? COULEUR_GRISE : COULEURS_SERIE[s];
    lv_obj_set_style_text_color(chauffant_bouton_label(cible), lv_color_hex(couleur), 0);
}

/* Cree UN bouton de ligne de chauffant (NOM ou VALEUR) -- meme idiome que les
 * tuiles de menu de la colonne droite (voir la boucle de creation de
 * `zone_menu` plus bas) : `lv_button_create()` (deja LV_OBJ_FLAG_CLICKABLE
 * par defaut) + `lv_obj_remove_style_all()` (theme par defaut + transition
 * animee otes, meme raison que ecran_menu_reglages.c) + fond COULEUR_BOUTON/
 * coins arrondis poses a la main + un UNIQUE `lv_label_t` enfant, CENTRE
 * (`lv_obj_center()`) plutot que stocke a part -- retrouve via
 * chauffant_bouton_label() ci-dessus. `x`/`largeur` distinguent le bouton NOM
 * (etroit, a gauche de la ligne) du bouton VALEUR (le reste de la colonne, a
 * droite) ; les deux partagent CHAUFFANT_LIGNE_HAUTEUR (>= 44px, cible
 * tactile, _Static_assert plus haut). La position Y REELLE est posee par
 * mettre_a_jour() (chauffants presents repositionnes de facon contigue a
 * chaque appel, voir son commentaire) -- 0 ici n'est qu'un placeholder tant
 * que la ligne reste masquee. */
static lv_obj_t *chauffant_bouton_creer(lv_obj_t *parent, lv_coord_t x, lv_coord_t largeur)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_remove_style_all(bouton);
    lv_obj_set_size(bouton, largeur, CHAUFFANT_LIGNE_HAUTEUR);
    lv_obj_set_pos(bouton, x, 0);
    lv_obj_set_style_bg_opa(bouton, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 8, 0);
    lv_obj_clear_flag(bouton, LV_OBJ_FLAG_SCROLLABLE); /* un seul label, tient toujours dans le bouton */

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(label, "");
    lv_obj_center(label);

    return bouton;
}

/* Cree `ctx->statut`, la ligne de statut pleine largeur SOUS les deux
 * colonnes (tache de suivi, refinement 2) -- positionnee a MARGE (== GAUCHE_X,
 * le meme bord gauche que le contenu des deux colonnes au-dessus) et
 * STATUT_Y, sans largeur fixee explicitement (auto-dimensionnee au contenu,
 * meme choix que l'ancien resume compact : le texte reste largement sous
 * LARGEUR_CONTENU - 2*MARGE meme au pire cas, jamais besoin d'un
 * LV_LABEL_LONG_DOT/CLIP qui exigerait une passe de layout resolue, voir le
 * commentaire de l'ancien resume dans construire() pour la meme raison
 * ailleurs dans ce fichier). */
static lv_obj_t *statut_ligne_creer(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(label, "");
    lv_obj_set_pos(label, MARGE, STATUT_Y);
    return label;
}

/* --- Colonne droite : chaque tuile navigue reellement vers un ecran deja
 * construit par un jalon precedent -- aucune case no-op ici, contrairement a
 * l'ancien contenu de ce fichier a ses tout premiers jalons. Echec
 * (ESP_ERR_NO_MEM, pile deja pleine) delibrement ignore, meme raison que
 * menu_reglages_cb_##symbole() de ecran_menu_reglages.c : ce hub n'a rien de
 * plus utile a journaliser qu'un simple "rien ne se passe", et la pile est
 * bornee a NAVIGATION_PROFONDEUR_MAX -- un hub-vers-panneau est toujours une
 * profondeur 1->2, jamais pres de cette borne en usage normal. --------- */
static void ouvrir_homing_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_HOMING);
}

static void ouvrir_temperature_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_TEMPERATURES);
}

static void ouvrir_actions_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_ACTIONS);
}

static void ouvrir_configuration_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_MENU_REGLAGES);
}

static void ouvrir_print_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_FICHIERS);
}

/* Feature "Impression depuis USB", tache B : 6e tuile, meme idiome que les
 * cinq autres (aucune case no-op). */
static void ouvrir_usb_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_USB);
}

typedef void (*menu_cb_t)(lv_event_t *e);

static const struct {
    const char *titre;
    menu_cb_t   cb;
} MENU_DEFS[ECRAN_ACCUEIL_HUB_MENU_NB] = {
    [ECRAN_ACCUEIL_HUB_MENU_HOMING]        = { "Homing",        ouvrir_homing_cb },
    [ECRAN_ACCUEIL_HUB_MENU_TEMPERATURE]   = { "Temperature",   ouvrir_temperature_cb },
    [ECRAN_ACCUEIL_HUB_MENU_ACTIONS]       = { "Actions",       ouvrir_actions_cb },
    [ECRAN_ACCUEIL_HUB_MENU_CONFIGURATION] = { "Configuration", ouvrir_configuration_cb },
    [ECRAN_ACCUEIL_HUB_MENU_PRINT]         = { "Print",         ouvrir_print_cb },
    [ECRAN_ACCUEIL_HUB_MENU_USB]           = { "USB",           ouvrir_usb_cb },
};

static void ecran_accueil_hub_construire(lv_obj_t *parent, void *contexte)
{
    ecran_accueil_hub_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- toutes les series demarrent VISIBLES (spec, task-5-brief.md :
     * "tout a true au depart") -- pose ICI, avant toute creation de widget,
     * comme derniere_gen plus bas : un etat qu'un clic peut faire evoluer
     * mais que construire() n'a besoin d'ecrire qu'une seule fois. --------- */
    for (uint8_t i = 0; i < KLIPPER_HISTO_SERIES; i++) {
        ctx->serie_visible[i] = true;
    }
    ctx->donnees_perimees = false; /* premiere donnee jamais recue = pas encore perimee, reecrit au premier mettre_a_jour() */

    /* --- colonne gauche : lignes de chauffants -- `zone_chauffants` est un
     * conteneur SCROLLABLE a hauteur FIXE (CHAUFFANTS_ZONE_HAUTEUR, juste
     * assez pour CHAUFFANT_LIGNES_VISIBLES lignes-boutons) qui porte le pool
     * ENTIER (ECRAN_ACCUEIL_HUB_HEATER_LIGNES == KLIPPER_HISTO_SERIES == 9),
     * masque/rempli/repositionne par mettre_a_jour() selon le nombre de
     * chauffants reellement presents -- voir le commentaire de tete du .h.
     * `lv_obj_remove_style_all()` + restyle manuel de LV_PART_SCROLLBAR : la
     * barre de defilement doit rester VISIBLE (spec : "with a visible
     * scrollbar") malgre le theme par defaut retire, meme raison que le
     * fond/bordure de `bouton` dans chauffant_bouton_creer() plus haut. ---- */
    ctx->zone_chauffants = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_chauffants);
    lv_obj_set_pos(ctx->zone_chauffants, GAUCHE_X, CHAUFFANTS_ZONE_Y);
    lv_obj_set_size(ctx->zone_chauffants, GAUCHE_LARGEUR, CHAUFFANTS_ZONE_HAUTEUR);
    lv_obj_set_style_bg_opa(ctx->zone_chauffants, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->zone_chauffants, 0, 0);
    lv_obj_set_style_pad_all(ctx->zone_chauffants, 0, 0);
    lv_obj_add_flag(ctx->zone_chauffants, LV_OBJ_FLAG_SCROLLABLE); /* defensif : deja pose par lv_obj_create() */
    lv_obj_set_scroll_dir(ctx->zone_chauffants, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->zone_chauffants, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(ctx->zone_chauffants, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(ctx->zone_chauffants, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(ctx->zone_chauffants, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(ctx->zone_chauffants, 4, LV_PART_SCROLLBAR);

    /* `chauffant_valeur[i]` ET `chauffant_nom[i]` sont des BOUTONS
     * (chauffant_bouton_creer(), deja LV_OBJ_FLAG_CLICKABLE) -- deux rappels
     * DISTINCTS sur le MEME element `chauffant_infos[i]`, jamais confondus
     * (voir le commentaire de tete du fichier, taches 4 et 5). Masquees ICI
     * par defaut : avant le premier mettre_a_jour(), aucun chauffant n'est
     * connu present, montrer le pool entier (9 lignes vides) serait un
     * defilement transitoire sans interet -- mettre_a_jour() les demasque et
     * les repositionne de facon contigue (0..total-1), voir son commentaire
     * plus bas. ------------------------------------------------------------ */
    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        lv_obj_t *nom = chauffant_bouton_creer(ctx->zone_chauffants, 0, CHAUFFANT_NOM_LARGEUR);
        ctx->chauffant_nom[i] = nom;

        lv_obj_t *valeur = chauffant_bouton_creer(ctx->zone_chauffants, CHAUFFANT_VALEUR_X, CHAUFFANT_VALEUR_LARGEUR);
        ctx->chauffant_valeur[i] = valeur;

        lv_obj_add_flag(nom, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(valeur, LV_OBJ_FLAG_HIDDEN);

        /* `ctx` pose ICI, une fois pour toutes -- meme idiome que
         * ctx->cellule_infos[i].ctx dans ecran_temperatures.c ("jamais NULL
         * une fois construire() passe"). est_plateau/indice_extrudeur/
         * consigne_courante restent a leur valeur zero-initialisee (calloc du
         * contexte, voir ecran.h) tant que mettre_a_jour() n'a pas tourne au
         * moins une fois pour CETTE ligne -- inoffensif, la ligne est masquee
         * jusque-la. */
        ctx->chauffant_infos[i].ctx = ctx;
        lv_obj_add_event_cb(valeur, chauffant_valeur_cb, LV_EVENT_CLICKED, &ctx->chauffant_infos[i]);

        /* Tache 5 : meme `chauffant_infos[i]`, un SECOND rappel sur le
         * bouton NOM -- voir chauffant_nom_cb() plus haut pour ce qu'il en
         * lit (est_plateau/indice_extrudeur, jamais consigne_courante). */
        lv_obj_add_event_cb(nom, chauffant_nom_cb, LV_EVENT_CLICKED, &ctx->chauffant_infos[i]);
    }

    /* --- ligne de statut pleine largeur, SOUS les deux colonnes -- tache de
     * suivi (refinement 2), remplace l'ancien resume compact trois lignes de
     * la colonne gauche (voir statut_ligne_creer()/STATUT_Y plus haut).
     * JAMAIS masquee (contrairement a l'ancienne ligne `progression`) : la
     * sous-chaine de progression est simplement absente du texte hors
     * impression plutot qu'un widget separe qu'on cache/montre, voir
     * mettre_a_jour(). PAS de LV_LABEL_LONG_DOT/CLIP ici, meme raison que
     * l'ancien resume (git, avant cette reecriture) : son calcul de
     * troncature lit les coordonnees RESOLUES de l'objet, qui exigeraient une
     * passe de layout entre construire() et le premier mettre_a_jour() --
     * jamais declenchee ici, les deux s'enchainent directement y compris
     * cote host-test. ------------------------------------------------------- */
    ctx->statut = statut_ligne_creer(parent);

    /* --- colonne gauche : graphe d'historique -- une serie par chauffant
     * PRESENT au moment de cet appel (jamais ajoutee/retiree ensuite, voir
     * le commentaire de tete du .h), backfillee depuis le store (tache 1)
     * SANS JAMAIS copier le tampon entier -- un seul tampon LOCAL de
     * KLIPPER_HISTO_POINTS points (240 octets), reutilise serie par serie. */
    ctx->chart = lv_chart_create(parent);
    lv_obj_remove_style_all(ctx->chart);
    lv_obj_set_style_bg_color(ctx->chart, lv_color_hex(COULEUR_FOND_CHART), 0);
    lv_obj_set_style_bg_opa(ctx->chart, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ctx->chart, 8, 0);
    /* Largeur de trait seule : la couleur de LV_PART_ITEMS est de toute
     * facon ecrasee par la couleur PROPRE de chaque serie au dessin (voir
     * lv_chart.c, chart_draw_series_line() -- `line_dsc.color = ser->color`
     * inconditionnel), la fixer ici serait une configuration morte. */
    lv_obj_set_style_line_width(ctx->chart, 2, LV_PART_ITEMS);
    lv_obj_set_pos(ctx->chart, GAUCHE_X, CHART_Y);
    lv_obj_set_size(ctx->chart, GAUCHE_LARGEUR, CHART_HAUTEUR);
    lv_chart_set_type(ctx->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ctx->chart, KLIPPER_HISTO_POINTS);
    lv_chart_set_update_mode(ctx->chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(ctx->chart, LV_CHART_AXIS_PRIMARY_Y, CHART_Y_MIN, CHART_Y_MAX);
    lv_chart_set_div_line_count(ctx->chart, 3, 0);

    for (uint8_t i = 0; i < KLIPPER_HISTO_SERIES; i++) {
        if (klipper_temp_historique_serie_presente(i)) {
            chart_ajouter_serie(ctx, i);
        } else {
            ctx->serie[i] = NULL; /* rattrapee par mettre_a_jour() si ce chauffant apparait plus tard, voir chart_ajouter_serie() */
        }
    }
    /* Capturee APRES le backfill : le premier mettre_a_jour() ne doit pas
     * re-ajouter le dernier point que le backfill vient deja d'inclure. */
    ctx->derniere_gen = klipper_temp_historique_generation();

    /* --- colonne droite : six tuiles empilees verticalement (voir
     * ECRAN_ACCUEIL_HUB_MENU_* -- meme idiome que la grille de menu de
     * l'ancien contenu de ce fichier, une seule colonne au lieu d'une
     * rangee). ------------------------------------------------------------ */
    ctx->zone_menu = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_menu);
    lv_obj_clear_flag(ctx->zone_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->zone_menu, DROITE_LARGEUR, DROITE_ZONE_HAUTEUR);
    lv_obj_set_pos(ctx->zone_menu, DROITE_X, CONTENU_Y);

    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        lv_coord_t y = (lv_coord_t)(i * (TUILE_HAUTEUR + TUILE_ECART));

        lv_obj_t *bouton = lv_button_create(ctx->zone_menu);
        /* lv_obj_remove_style_all() : meme raison que dans
         * ecran_menu_reglages.c -- theme par defaut + transition animee
         * otes, pour ne pas alourdir style_trans_ll cote host-test. */
        lv_obj_remove_style_all(bouton);
        lv_obj_set_size(bouton, DROITE_LARGEUR, TUILE_HAUTEUR);
        lv_obj_set_pos(bouton, 0, y);
        lv_obj_set_style_bg_opa(bouton, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
        lv_obj_set_style_border_width(bouton, 0, 0);
        lv_obj_set_style_shadow_width(bouton, 0, 0);
        lv_obj_set_style_radius(bouton, 10, 0);

        lv_obj_t *label = lv_label_create(bouton);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, MENU_DEFS[i].titre);
        lv_obj_center(label);

        lv_obj_add_event_cb(bouton, MENU_DEFS[i].cb, LV_EVENT_CLICKED, NULL);

        ctx->menu_boutons[i] = bouton;
    }
}

static void ecran_accueil_hub_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_accueil_hub_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Defense contre un etat corrompu/malforme, meme garde que l'ancien
     * contenu de ce fichier. */
    uint8_t nb_extrudeurs = e->nb_extrudeurs;
    if (nb_extrudeurs > KLIPPER_EXTRUDEURS_MAX) {
        nb_extrudeurs = KLIPPER_EXTRUDEURS_MAX;
    }

    /* --- lignes de chauffants : extrudeurs presents puis plateau, dans cet
     * ordre, bornes a ECRAN_ACCUEIL_HUB_HEATER_LIGNES (le pool ENTIER, pas
     * une visibilite bornee -- voir le commentaire de tete du .h) --
     * systematique a chaque appel (les lignes au-dela du nombre present sont
     * masquees, jamais un etat fige depuis le premier passage). Chaque ligne
     * PRESENTE (indice `total`) est en plus REPOSITIONNEE ici (lv_obj_set_y())
     * a total*(CHAUFFANT_LIGNE_HAUTEUR+CHAUFFANT_LIGNE_ECART) -- c'est ce qui
     * garde les lignes presentes CONTIGUES (0..total-1) meme quand un
     * chauffant intermediaire disparait d'un appel a l'autre, et donc AUCUN
     * trou de defilement dans `zone_chauffants` (les lignes masquees,
     * au-dela, n'ont pas besoin d'etre repositionnees -- LVGL les ignore deja
     * pour l'etendue scrollable, voir le commentaire de tete du .h). --------- */
    uint8_t total = 0;
    char    valeur[16];
    char    consigne[16];
    char    nom[8];
    char    texte_valeur[40];

    for (uint8_t i = 0; i < nb_extrudeurs && total < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        if (!e->extrudeurs[i].presente) {
            continue;
        }
        snprintf(nom, sizeof(nom), "T%u", (unsigned)i);
        ui_format_temperature(valeur, sizeof(valeur), e->extrudeurs[i].actuelle);
        ui_format_temperature(consigne, sizeof(consigne), e->extrudeurs[i].consigne);
        snprintf(texte_valeur, sizeof(texte_valeur), "%s/%s", valeur, consigne);
        lv_label_set_text(chauffant_bouton_label(ctx->chauffant_nom[total]), nom);
        lv_label_set_text(chauffant_bouton_label(ctx->chauffant_valeur[total]), texte_valeur);
        lv_coord_t y = (lv_coord_t)(total * (CHAUFFANT_LIGNE_HAUTEUR + CHAUFFANT_LIGNE_ECART));
        lv_obj_set_y(ctx->chauffant_nom[total], y);
        lv_obj_set_y(ctx->chauffant_valeur[total], y);
        /* Identite du chauffeur + consigne courante, relues par
         * chauffant_valeur_cb()/le clavier au moment du tap -- voir le
         * commentaire de ecran_accueil_hub_chauffant_info_t (le .h). */
        ctx->chauffant_infos[total].est_plateau = false;
        ctx->chauffant_infos[total].indice_extrudeur = i;
        ctx->chauffant_infos[total].consigne_courante = consigne_u16(e->extrudeurs[i].consigne);
        total++;
    }
    if (e->plateau.presente && total < ECRAN_ACCUEIL_HUB_HEATER_LIGNES) {
        ui_format_temperature(valeur, sizeof(valeur), e->plateau.actuelle);
        ui_format_temperature(consigne, sizeof(consigne), e->plateau.consigne);
        snprintf(texte_valeur, sizeof(texte_valeur), "%s/%s", valeur, consigne);
        lv_label_set_text(chauffant_bouton_label(ctx->chauffant_nom[total]), "Bed");
        lv_label_set_text(chauffant_bouton_label(ctx->chauffant_valeur[total]), texte_valeur);
        lv_coord_t y = (lv_coord_t)(total * (CHAUFFANT_LIGNE_HAUTEUR + CHAUFFANT_LIGNE_ECART));
        lv_obj_set_y(ctx->chauffant_nom[total], y);
        lv_obj_set_y(ctx->chauffant_valeur[total], y);
        ctx->chauffant_infos[total].est_plateau = true;
        ctx->chauffant_infos[total].indice_extrudeur = 0; /* non utilise, est_plateau=true */
        ctx->chauffant_infos[total].consigne_courante = consigne_u16(e->plateau.consigne);
        total++;
    }
    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        if (i < total) {
            lv_obj_clear_flag(ctx->chauffant_nom[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ctx->chauffant_valeur[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->chauffant_nom[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ctx->chauffant_valeur[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* --- ligne de statut pleine largeur : position + outil actif + vitesse/
     * flux + progression, SOUS les deux colonnes (tache de suivi, refinement
     * 2 -- remplace les trois lignes empilees de l'ancien resume compact,
     * voir ecran_accueil_hub_ctx_t::statut dans le .h). "--" par axe non
     * reference (formater_axe(), meme idiome que ecran_deplacer_mettre_a_jour())
     * ; le segment de progression est simplement ABSENT du texte hors
     * impression -- jamais un widget separe qu'on masque/demasque comme
     * l'ancienne ligne `progression` (re-evalue a CHAQUE appel, systematique,
     * meme discipline que bouton_macros dans ecran_accueil.c : une
     * impression qui se termine ne doit pas laisser un pourcentage perime
     * affiche). isnan()/isinf() d'abord, meme garde-fou que
     * progression_definir() (progression.c) -- `e->progression` vient de
     * Moonraker, une source qui peut renvoyer n'importe quoi pendant un
     * redemarrage de klippy. -------------------------------------------- */
    bool axe_x_reference = (e->axes_references & 0x1u) != 0;
    bool axe_y_reference = (e->axes_references & 0x2u) != 0;
    bool axe_z_reference = (e->axes_references & 0x4u) != 0;
    char pos_x[8];
    char pos_y[8];
    char pos_z[8];
    formater_axe(pos_x, sizeof(pos_x), e->position[0], axe_x_reference);
    formater_axe(pos_y, sizeof(pos_y), e->position[1], axe_y_reference);
    formater_axe(pos_z, sizeof(pos_z), e->position[2], axe_z_reference);

    char texte_statut[96];
    int ecrit;
    if (nb_extrudeurs > 0) {
        ecrit = snprintf(texte_statut, sizeof(texte_statut), "X:%s Y:%s Z:%s  T%u  Speed: %u%%  Flow: %u%%", pos_x,
                          pos_y, pos_z, (unsigned)e->outil_actif, (unsigned)e->vitesse_pct, (unsigned)e->flux_pct);
    } else {
        ecrit = snprintf(texte_statut, sizeof(texte_statut), "X:%s Y:%s Z:%s  T--  Speed: %u%%  Flow: %u%%", pos_x,
                          pos_y, pos_z, (unsigned)e->vitesse_pct, (unsigned)e->flux_pct);
    }
    if (e->impression_en_cours && ecrit > 0 && (size_t)ecrit < sizeof(texte_statut)) {
        float fraction = e->progression;
        if (isnan(fraction) || isinf(fraction)) {
            fraction = (isinf(fraction) && fraction > 0.0f) ? 1.0f : 0.0f;
        } else if (fraction < 0.0f) {
            fraction = 0.0f;
        } else if (fraction > 1.0f) {
            fraction = 1.0f;
        }
        unsigned pct = (unsigned)(fraction * 100.0f + 0.5f);
        snprintf(texte_statut + ecrit, sizeof(texte_statut) - (size_t)ecrit, "  Printing: %u%%", pct);
    }
    lv_label_set_text(ctx->statut, texte_statut);

    /* --- grisage integral du resume (chauffants + ligne de statut), style
     * RESOLU (spec C3, ecran.h) -- systematique a chaque appel, jamais
     * incremental (meme lecon que tuile_griser()/l'ancien contenu de ce
     * fichier). Le graphe et la grille de menu, eux, NE SONT JAMAIS grises --
     * le graphe reste une trace historique valable meme sur un etat courant
     * perime, et chaque tuile de menu ouvre un ecran qui grise lui-meme son
     * propre contenu si besoin (meme choix delibere que l'ancien hub :
     * "naviguer... reste sans danger meme avec un etat perime"). ---------
     *
     * Label NOM (tache 5, task-5-brief.md ; couleur de courbe ajoutee dans une
     * tache de suivi) : SEUL cas ou ce grisage C3 ne suffit pas seul --
     * chauffant_nom_cb() (plus haut) grise/recolore CE MEME label
     * independamment, au clic, selon `serie_visible[]`. Les deux mecanismes
     * ne doivent JAMAIS s'ecraser l'un l'autre : la regle (identique cote
     * chauffant_nom_cb()) est
     *   grise                si (donnees_perimees OU courbe masquee)
     *   couleur de SA courbe seulement si (fraiches ET courbe visible)
     * (COULEURS_SERIE[s] -- meme mapping que chauffant_info_serie_indice(),
     * meme tableau que celui qui colore la courbe sur le chart) -- appliquee
     * ICI a CHAQUE rafraichissement en relisant `ctx->serie_visible[]` (que
     * seul un clic fait evoluer, jamais cette fonction), donc un
     * mettre_a_jour() "frais" ne recolore jamais un nom dont l'utilisateur a
     * masque la courbe, et un toggle ulterieur reste grise tant que les
     * donnees restent perimees. `chauffant_valeur[i]`, lui, N'EST PAS
     * concerne (spec tache 5 : "Do NOT change the VALUE-tap behaviour") -- il
     * garde la couleur COMMUNE `couleur` ci-dessous, comme avant cette tache.
     * `ctx->donnees_perimees` est memorise ICI (dernier argument recu) pour
     * que chauffant_nom_cb() applique la MEME regle immediatement au clic,
     * sans attendre le prochain appel. -------------------------------------- */
    ctx->donnees_perimees = donnees_perimees;
    uint32_t couleur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        uint8_t s = chauffant_info_serie_indice(&ctx->chauffant_infos[i]);
        uint32_t couleur_nom = (donnees_perimees || !ctx->serie_visible[s]) ? COULEUR_GRISE : COULEURS_SERIE[s];
        lv_obj_set_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[i]), lv_color_hex(couleur_nom), 0);
        lv_obj_set_style_text_color(chauffant_bouton_label(ctx->chauffant_valeur[i]), lv_color_hex(couleur), 0);
    }
    lv_obj_set_style_text_color(ctx->statut, lv_color_hex(couleur), 0);

    /* --- graphe : rafraichi UNIQUEMENT quand le store (tache 1) a avance
     * depuis le dernier appel -- l'echantillonneur pousse un point toutes
     * les 5 s (app_main.c/simulateur/main.c), largement plus lent que la
     * cadence d'appel de cette fonction (~200 ms, voir habillage_pomper()) :
     * sans ce garde-fou, ce chart redessinerait ~25x pour rien entre deux
     * points reels. */
    uint32_t generation = klipper_temp_historique_generation();
    if (generation != ctx->derniere_gen) {
        for (uint8_t i = 0; i < KLIPPER_HISTO_SERIES; i++) {
            if (ctx->serie[i] == NULL) {
                /* Chauffant apparu APRES construire() (voir chart_ajouter_serie()
                 * pour le cas reel qui declenche ceci au boot) : rattrapage
                 * unique, cree la serie et la backfille d'un coup plutot que
                 * de la laisser demarrer vide et ne grossir que point par
                 * point a partir de maintenant. */
                if (klipper_temp_historique_serie_presente(i)) {
                    chart_ajouter_serie(ctx, i);
                }
                continue;
            }
            int16_t dernier;
            if (klipper_temp_historique_dernier(i, &dernier)) {
                lv_chart_set_next_value(ctx->chart, ctx->serie[i], dernier);
            }
        }
        lv_chart_refresh(ctx->chart);
        ctx->derniere_gen = generation;
    }
}

const ecran_desc_t ECRAN_ACCUEIL_HUB = {
    .id = "accueil_hub",
    .titre = "Home",
    .taille_contexte = sizeof(ecran_accueil_hub_ctx_t),
    .construire = ecran_accueil_hub_construire,
    .mettre_a_jour = ecran_accueil_hub_mettre_a_jour,
    .detruire = NULL,
};
