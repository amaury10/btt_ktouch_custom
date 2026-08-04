/* Implémentation : voir ecran_accueil.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : deux tuiles
 * de température en haut, le nom de fichier juste dessous, une barre de
 * progression pleine largeur, trois boutons en bas. Toutes les constantes de
 * position sont dérivées les unes des autres (voir les macros ci-dessous)
 * plutôt que recopiées, pour qu'un futur ajustement de l'une d'entre elles ne
 * désaligne pas silencieusement le reste de la colonne. */
#include "ecran_accueil.h"

#include <stdio.h>
#include <string.h>

#include "backend.h"
#include "confirmation.h"
#include "ecran_macros.h"
#include "ecran_reglage_fin.h" /* ECRAN_REGLAGE_FIN, tache 5 */
#include "etat_klipper.h"
#include "habillage.h"
#include "miniature.h" /* feature "Miniatures gcode", tache B */
#include "navigation.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE          20
/* Rangée symétrique : marge gauche + tuile + écart + tuile + marge droite,
 * les TROIS occurrences de MARGE (voir le _Static_assert plus bas). Fix
 * défaut 2 (revue live jalon 3a) : à 800px, 380 pilait exactement le bord
 * droit de l'écran (MARGE + 380 + MARGE + 380 = 800, sans marge droite --
 * la tuile Bed touchait le boîtier), passé inaperçu parce que l'ancien
 * _Static_assert ne comptait que DEUX MARGE au lieu de trois et laissait
 * donc passer une marge droite nulle sans jamais le signaler ; 370
 * restituait une marge droite de 20 px, identique à la marge gauche.
 * Sous-projet 5 (conversion 742, rail persistant -- voir LARGEUR_CONTENU
 * ci-dessus) : (742 - 3*MARGE)/2 = 341 conserve exactement cette même
 * symétrie, marge droite = marge gauche = 20 px.
 *
 * Feature "Miniatures gcode", tâche B (intégration ESP) : la rangée de
 * tuiles cède désormais sa portion droite à un troisième élément, la
 * miniature (MINIATURE_LARGEUR/MINIATURE_HAUTEUR plus bas) -- TUILE_LARGEUR
 * n'est donc plus une constante à recopier, mais DÉRIVÉE de la largeur
 * disponible une fois MINIATURE_LARGEUR et ses QUATRE marges (gauche, entre
 * les deux tuiles, entre la 2e tuile et la miniature, droite) retirées ; même
 * philosophie que le reste de ce fichier ("les macros dérivées empêchent un
 * désalignement silencieux", voir le commentaire des _Static_assert plus
 * bas). (742 - 4*20 - 120) / 2 = 271 -- toujours largement assez pour
 * "205.0"/"100.0" en Montserrat 48 (341 d'origine avait de la marge à
 * revendre). */
#define MINIATURE_LARGEUR 120
#define MINIATURE_HAUTEUR 120
#define TUILE_LARGEUR ((LARGEUR_CONTENU - 4 * MARGE - MINIATURE_LARGEUR) / 2)
#define TUILE_HAUTEUR 140
#define TUILE_Y        16
/* Miniature : alignée en haut avec les tuiles, à leur droite -- voir
 * MINIATURE_LARGEUR ci-dessus pour la dérivation de TUILE_LARGEUR qui lui
 * laisse exactement la place. */
#define MINIATURE_X (3 * MARGE + 2 * TUILE_LARGEUR)
#define MINIATURE_Y TUILE_Y

#define FICHIER_Y       (TUILE_Y + TUILE_HAUTEUR + 10)
#define FICHIER_HAUTEUR  30

#define PROGRESSION_Y       (FICHIER_Y + FICHIER_HAUTEUR + 10)
#define PROGRESSION_HAUTEUR  40
#define PROGRESSION_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)
/* Décalage horizontal du libellé de temps restant par rapport au CENTRE de
 * la barre, où vit déjà le pourcentage (posé par progression_creer(), voir
 * progression.h) : assez grand pour ne jamais chevaucher "100.0%", le texte
 * de pourcentage le plus large possible. */
#define TEMPS_DECALAGE_X 120

#define BOUTONS_Y      (PROGRESSION_Y + PROGRESSION_HAUTEUR + 20)
/* Sous-projet 5 (conversion 742, rail persistant -- voir LARGEUR_CONTENU
 * ci-dessus) : 230 (hérité du contenu 800px) fait déborder la rangée des
 * trois boutons (_Static_assert plus bas : MARGE + 3*BOUTON_LARGEUR +
 * 2*BOUTON_ECART + MARGE = 20 + 690 + 70 + 20 = 800 > 742). 210 -- ajustement
 * minimal, BOUTON_ECART inchangé -- ramène la somme à 740 <= 742 ; boutons
 * toujours très largement >= 44px de cible tactile (210x70). */
#define BOUTON_LARGEUR 210
#define BOUTON_HAUTEUR  70
#define BOUTON_ECART    35

/* Tache 6 (jalon 3a) : bouton "Macros" -- une NOUVELLE rangee sous les trois
 * boutons de commande existants, jamais insere DANS cette rangee (les trois
 * boutons s'y partagent deja quasiment tout LARGEUR_CONTENU -- 740/742px,
 * voir le _Static_assert plus bas -- y ajouter un quatrieme aurait force a
 * retoucher une geometrie deja revue au jalon 2b).
 *
 * Tache 5 (sous-projet "refonte IHM KlipperScreen") : cette rangee n'est
 * plus SEULEMENT Macros -- elle se partage desormais en DEUX moities egales
 * avec le nouveau bouton "Fine Tune" (KlipperScreen le place dans le flux
 * d'impression, pas dans Configuration -- voir ecran_menu_reglages.c, qui ne
 * le referme plus). Pas de TROISIEME rangee pleine largeur possible : le bas
 * du contenu est deja serre (voir MACROS_BOUTON_Y, tout juste sous
 * HAUTEUR_CONTENU, _Static_assert plus bas). RANGEE2_BOUTON_LARGEUR reprenait
 * a l'origine TUILE_LARGEUR (meme largeur que les tuiles de temperature en
 * haut de l'ecran, meme ecart MARGE entre les deux) pour garder le meme
 * rythme visuel que la rangee de tuiles.
 *
 * Feature "Miniatures gcode", tache B : TUILE_LARGEUR a RETRECI pour laisser
 * la place a la miniature (voir sa definition plus haut) -- cette rangee-ci,
 * elle, N'A PAS de miniature a loger et n'a donc AUCUNE raison de retrecir
 * avec elle. RANGEE2_BOUTON_LARGEUR est donc desormais calculee
 * INDEPENDAMMENT, avec la MEME formule a DEUX boutons que TUILE_LARGEUR avait
 * a l'origine (742 - 3*MARGE)/2 -- plus le meme rythme visuel "gratuit" que
 * documente ci-dessus (les deux largeurs coincidaient par construction avant
 * cette feature), mais un calcul explicite plutot qu'une dependance
 * accidentelle a une autre rangee qui n'a plus rien a voir. */
#define MACROS_BOUTON_Y       (BOUTONS_Y + BOUTON_HAUTEUR + 20)
#define MACROS_BOUTON_HAUTEUR 60
#define RANGEE2_BOUTON_LARGEUR ((LARGEUR_CONTENU - 3 * MARGE) / 2)
#define RANGEE2_ECART           MARGE

#define FINE_TUNE_BOUTON_X (MARGE + RANGEE2_BOUTON_LARGEUR + RANGEE2_ECART)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* imposee par le brief : gris de peremption */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_BOUTON_URGENCE   0xE74C3C /* meme rouge que la pastille "hors ligne", habillage.c */
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Poids (0..255) de la couleur de base d'un bouton dans son melange vers
 * COULEUR_FOND pour obtenir la variante LV_STATE_DISABLED -- voir le
 * commentaire de bouton_creer() ci-dessous pour pourquoi ce melange doit
 * exister comme style local DEDIE, pas comme un simple recolorage. Faible
 * (90/255, ~35% de couleur de base) : le bouton doit se fondre visiblement
 * vers le panneau, y compris pour le rouge d'urgence. */
#define BOUTON_DESACTIVE_MELANGE 90

/* Le commentaire de tête promet que ces macros dérivées empêchent un
 * désalignement silencieux : ça ne vaut que si un débordement devient une
 * erreur de compilation plutôt qu'un pixel qui sort du cadre sans que
 * personne ne le remarque avant une capture (revue tâche 6, fix round 1,
 * M-asserts). Vérifié une fois ici, à la compilation, pour les deux
 * rangées dont la largeur est la somme de plusieurs constantes. */
/* Égalité stricte, pas juste "<=" (fix défaut 2, revue live jalon 3a) : une
 * inégalité laisse passer une marge droite nulle (ou négative, débordement
 * silencieux) sans jamais le signaler -- exactement le défaut qui a laissé
 * Bed toucher le bord droit de l'écran pendant tout le jalon précédent.
 * Trois occurrences de MARGE (gauche, écart entre tuiles, droite) : une
 * régression future de TUILE_LARGEUR vers 351 (2 tuiles + 2 MARGE pilent
 * déjà les 742px, sans laisser de place à une troisième MARGE) fait échouer
 * cette assertion à la compilation plutôt que de laisser un pixel sortir du
 * cadre sans que personne ne le remarque avant une capture. */
/* Feature "Miniatures gcode", tâche B : QUATRE occurrences de MARGE
 * désormais (gauche, entre les deux tuiles, entre la 2e tuile et la
 * miniature, droite -- voir MINIATURE_LARGEUR/TUILE_LARGEUR ci-dessus), plus
 * MINIATURE_LARGEUR lui-même. Remplace l'ancienne assertion à trois MARGE
 * (qui datait d'avant la miniature) -- même égalité STRICTE, même raison
 * (fix défaut 2, revue live jalon 3a, voir le commentaire ci-dessus). */
_Static_assert(4 * MARGE + 2 * TUILE_LARGEUR + MINIATURE_LARGEUR == LARGEUR_CONTENU,
                "la rangee de tuiles + miniature n'est plus symetrique (marges incoherentes)");
/* La miniature elle-même ne doit jamais recouvrir la barre de progression
 * (PROGRESSION_Y, plus bas dans ce fichier) ni la bande de notification
 * persistante (habillage.c : ancrée au bas de l'écran, HAUTEUR_ECRAN(480) -
 * BANDEAU_HAUTEUR(60) = absolu y=420..480 -- soit local y=376..436 ici, ce
 * contenu démarrant à l'absolu y=BARRE_HAUTEUR(44), voir LARGEUR_CONTENU/
 * HAUTEUR_CONTENU en tête de fichier). Vérifié ici, à la compilation, comme
 * les autres garde-fous de non-recouvrement de ce fichier -- la miniature
 * étant alignée en haut de l'écran (MINIATURE_Y == TUILE_Y == 16), cette
 * marge est énorme, mais l'assertion reste la preuve, pas la conviction. */
#define BANDEAU_Y_LOCAL 376 /* 420 (absolu, habillage.c) - BARRE_HAUTEUR(44) */
_Static_assert(MINIATURE_Y + MINIATURE_HAUTEUR <= PROGRESSION_Y,
                "la miniature deborde sur la barre de progression");
_Static_assert(MINIATURE_Y + MINIATURE_HAUTEUR <= BANDEAU_Y_LOCAL,
                "la miniature deborde sur la bande de notification (absolu y>=420)");
_Static_assert(MARGE + 3 * BOUTON_LARGEUR + 2 * BOUTON_ECART + MARGE <= LARGEUR_CONTENU,
                "les trois boutons + marges/ecarts debordent de la largeur du contenu");
_Static_assert(MACROS_BOUTON_Y + MACROS_BOUTON_HAUTEUR <= HAUTEUR_CONTENU,
                "la rangee Macros/Fine Tune deborde de la hauteur du contenu");
/* Tache 5 : meme garde-fou de symetrie que la rangee de tuiles ci-dessus,
 * applique a la rangee Macros/Fine Tune -- deux boutons de meme largeur,
 * un seul ecart entre eux, marge gauche == marge droite. */
_Static_assert(2 * MARGE + 2 * RANGEE2_BOUTON_LARGEUR + RANGEE2_ECART == LARGEUR_CONTENU,
                "la rangee Macros/Fine Tune n'est plus symetrique (marge gauche != marge droite)");

/* Construit le bouton lui-même (taille, position, couleur, libellé initial) ;
 * ne pose AUCUN lv_obj_add_event_cb() -- c'est le travail de l'appelant, dans
 * ecran_accueil_construire() plus bas, une fois `ctx` disponible pour servir
 * de user_data aux trois rappels (voir la tâche 9 et le commentaire de tête
 * de ecran_accueil.h). */
/* CRITICAL (revue finale jalon 2b) : lv_obj_set_style_bg_color(bouton, ...,
 * 0) ci-dessous pose un style LOCAL a LV_STATE_DEFAULT. En LVGL 9, un style
 * local l'emporte sur tout style de theme quel que soit l'etat de l'objet --
 * sans un style local DEDIE a LV_STATE_DISABLED, poser LV_STATE_DISABLED sur
 * `bouton` (voir mettre_a_jour() plus bas) ne change pas la couleur de fond
 * resolue. Precision de la re-revue finale : le theme conserve UNE voie vers
 * un bouton desactive -- son style `disabled` est un FILTRE de couleur gris
 * a 50% (lv_theme_default.c), applique au dessin via les accesseurs
 * _filtered, et il est de surcroit ANIME (~80 ms + 70 ms de delai) ; les
 * captures "0 pixel de difference" de la revue etaient donc aussi un
 * artefact d'horloge LVGL jamais avancee. Il n'empeche : un lien mort
 * affichait des boutons pleinement armes (ou a peine delaves, une fois le
 * temps ecoule) qui ne repondaient plus au toucher, exactement le contraire
 * de ce que promettait ce fichier. Le correctif enregistre un
 * DEUXIEME style local, a LV_STATE_DISABLED cette fois (meme tier "local",
 * mais plus de bits d'etat en commun avec l'etat courant de l'objet quand il
 * est desactive : il gagne la resolution sans avoir a retirer le style
 * DEFAULT). Meme raisonnement pour le label : son texte blanc est lui aussi
 * un style local DEFAULT, donc grise a LV_STATE_DISABLED exige la meme paire
 * de styles -- et exige que le LABEL LUI-MEME porte l'etat DISABLED (les
 * etats LVGL ne se propagent pas automatiquement aux enfants), voir
 * mettre_a_jour() qui bascule desormais le bouton ET son label ensemble. */
static lv_obj_t *bouton_creer(lv_obj_t *parent, const char *texte, uint32_t couleur_fond, lv_coord_t x)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, BOUTON_LARGEUR, BOUTON_HAUTEUR);
    lv_obj_set_pos(bouton, x, BOUTONS_Y);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(couleur_fond), 0);
    lv_color_t couleur_desactivee =
        lv_color_mix(lv_color_hex(couleur_fond), lv_color_hex(COULEUR_FOND), BOUTON_DESACTIVE_MELANGE);
    lv_obj_set_style_bg_color(bouton, couleur_desactivee, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 10, 0);

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_GRISE), LV_STATE_DISABLED);
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    return bouton;
}

/* Bascule LV_STATE_DISABLED sur `bouton` ET sur son label enfant (voir le
 * commentaire de bouton_creer() ci-dessus : un etat LVGL ne se propage
 * jamais automatiquement a un enfant, donc le grisage du TEXTE exige que le
 * label porte lui-meme l'etat, pas seulement le bouton). Appele
 * systematiquement par mettre_a_jour(), jamais de facon incrementale --
 * meme discipline que tuile_griser()/progression_griser(). */
static void bouton_definir_desactive(lv_obj_t *bouton, bool desactive)
{
    lv_obj_t *label = lv_obj_get_child(bouton, 0);
    if (desactive) {
        lv_obj_add_state(bouton, LV_STATE_DISABLED);
        if (label != NULL) {
            lv_obj_add_state(label, LV_STATE_DISABLED);
        }
    } else {
        lv_obj_remove_state(bouton, LV_STATE_DISABLED);
        if (label != NULL) {
            lv_obj_remove_state(label, LV_STATE_DISABLED);
        }
    }
}

/* Tâche 9 : envoie `action` via ui_commander() (la seule porte, voir
 * ui/source_etat.h) et notifie un échec SYNCHRONE (file pleine, boucle pas
 * démarrée) avec `libelle` -- un mot anglais présentable, jamais l'action
 * interne francophone brute (BACKEND_ACTION_REPRENDRE vaut "reprendre").
 * L'échec ASYNCHRONE (commande acceptée ici, mais qui échoue plus tard à
 * l'exécution réelle par la boucle) est un chemin SÉPARÉ, remonté par
 * habillage_pomper() via ui_commande_echec() -- voir son commentaire dans
 * source_etat.h -- jamais depuis cet écran, qui n'a aucun moyen de connaître
 * ce résultat tardif (ui_commander() rend la main immédiatement, voir son
 * propre commentaire). N'écrit jamais l'état localement pour "faire réactif" :
 * le brief est explicite, un écran qui anticipe affiche du faux dès que la
 * commande échoue -- le prochain sondage (génération suivante) dira la
 * vérité. */
static void executer_commande(const char *action, const char *libelle)
{
    esp_err_t erreur = ui_commander(action, NULL);
    if (erreur != ESP_OK) {
        char texte[64];
        snprintf(texte, sizeof(texte), "Command failed: %s", libelle);
        habillage_notifier(texte, true);
    }
}

static void bouton_pause_cb(lv_event_t *e)
{
    ecran_accueil_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->donnees_perimees) {
        /* Garde défensive : LV_STATE_DISABLED (voir mettre_a_jour() plus bas)
         * bloque déjà un appui tactile réel (lv_indev.c vérifie cet état
         * avant de délivrer LV_EVENT_CLICKED), mais host-test/tests/
         * test_commandes.c envoie l'événement directement via
         * lv_obj_send_event(), qui ne passe jamais par cette vérification --
         * ce `if` est ce qui rend le grisage honnête même sous cette
         * simulation-là, pas seulement sous un vrai doigt. */
        return;
    }
    if (ctx->en_pause) {
        executer_commande(BACKEND_ACTION_REPRENDRE, "resume");
    } else {
        executer_commande(BACKEND_ACTION_PAUSE, "pause");
    }
}

static void rappel_confirmation_annuler(bool confirme, void *contexte)
{
    ecran_accueil_ctx_t *ctx = contexte;
    /* Fix round 1 (revue tache 9, HIGH) : NE PAS relire ctx->donnees_perimees
     * ici. Le dialogue reste ouvert le temps qu'un humain lise "This will
     * stop the current print" -- largement plus que les ~3 s (3 sondages
     * manques) qu'il faut a LIAISON_DEGRADEE pour s'installer. Avec la garde
     * ici, une liaison qui se degrade PENDANT que le dialogue est ouvert
     * faisait disparaitre une confirmation deja donnee sans rien empiler ni
     * rien notifier -- observe empiriquement par la revue : file 0->0,
     * bandeau intact. Une fois l'utilisateur confirme, la commande DOIT
     * partir ; si la liaison est reellement morte, ui_commander() l'accepte
     * quand meme (il ne connait que la profondeur de la file, jamais l'etat
     * de la liaison) et c'est backend_moonraker_commande() qui echouera a
     * l'execution reelle -- l'echec ASYNCHRONE remonte alors honnetement par
     * le sceau existant (ui_commande_echec(), voir habillage_pomper()), sans
     * qu'aucune commande n'ait jamais ete perdue en silence. La garde sur la
     * peremption reste a sa bonne place : bouton_annuler_cb() ci-dessous,
     * ou elle grise l'OUVERTURE du dialogue -- exactement le miroir de
     * LV_STATE_DISABLED. */
    if (!confirme || ctx == NULL) {
        return;
    }
    executer_commande(BACKEND_ACTION_ANNULER, "cancel");
}

static void rappel_confirmation_urgence(bool confirme, void *contexte)
{
    ecran_accueil_ctx_t *ctx = contexte;
    /* Fix round 1 (revue tache 9, HIGH) : meme raisonnement que
     * rappel_confirmation_annuler() ci-dessus, encore plus critique ici --
     * c'est le bouton d'ARRET D'URGENCE. Une imprimante en trouble, un
     * utilisateur qui tape E-STOP, lit "This will immediately halt the
     * printer", et la liaison qui se degrade PENDANT cette lecture ne doit
     * JAMAIS transformer une confirmation deja donnee en un dialogue qui se
     * ferme sans rien faire. */
    if (!confirme || ctx == NULL) {
        return;
    }
    executer_commande(BACKEND_ACTION_URGENCE, "emergency stop");
}

static void bouton_annuler_cb(lv_event_t *e)
{
    ecran_accueil_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->donnees_perimees) {
        return;
    }
    /* confirmation_ouvrir_ex(), pas confirmation_ouvrir() : le bouton d'action
     * lit deja "Cancel print", un bouton de declin par defaut "Cancel" ferait
     * deux boutons commencant tous les deux par le meme mot dans le meme
     * dialogue -- lequel annule vraiment l'impression ? (revue tache 9, fix
     * round 1, LOW). "Keep printing" dit sans ambiguite ce que ce bouton
     * fait reellement : rien, l'impression continue. */
    confirmation_ouvrir_ex("Cancel print?",
                            "This will stop the current print. This cannot be undone.",
                            "Cancel print", true, "Keep printing", rappel_confirmation_annuler, ctx);
}

static void bouton_urgence_cb(lv_event_t *e)
{
    ecran_accueil_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->donnees_perimees) {
        return;
    }
    confirmation_ouvrir("Emergency stop?",
                         "This will immediately halt the printer. This cannot be undone.",
                         "E-STOP", true, rappel_confirmation_urgence, ctx);
}

/* Tache 6 (jalon 3a) : navigue vers ECRAN_MACROS -- une navigation, pas une
 * commande (contrairement aux trois boutons ci-dessus), donc aucun passage
 * par ui_commander()/executer_commande() ici. Pas de garde sur
 * ctx->donnees_perimees non plus (contrairement aux trois autres) : la
 * navigation elle-meme reste sure hors ligne, et ECRAN_MACROS grise
 * integralement son propre contenu des l'arrivee (voir ecran_macros.c) --
 * le seul risque d'un bouton "actif" pendant une liaison perimee serait de
 * naviguer vers une liste de macros elle-meme deja grisee, jamais une
 * commande envoyee a tort. */
static void bouton_macros_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_MACROS);
}

/* Tache 5 (sous-projet "refonte IHM KlipperScreen") : navigue vers
 * ECRAN_REGLAGE_FIN -- meme raisonnement que bouton_macros_cb() ci-dessus,
 * une navigation, pas une commande, donc aucun passage par
 * ui_commander()/executer_commande() ni garde sur ctx->donnees_perimees :
 * ouvrir le panneau Fine Tune reste sans risque hors ligne, comme n'importe
 * quel autre panneau atteint depuis un menu (meme contrat que
 * ecran_menu_reglages.c, qui l'atteignait jusqu'ici). */
static void bouton_reglage_fin_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_REGLAGE_FIN);
}

static void ecran_accueil_construire(lv_obj_t *parent, void *contexte)
{
    ecran_accueil_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    tuile_creer(&ctx->buse, parent, "Nozzle");
    lv_obj_set_size(ctx->buse.racine, TUILE_LARGEUR, TUILE_HAUTEUR);
    lv_obj_set_pos(ctx->buse.racine, MARGE, TUILE_Y);

    tuile_creer(&ctx->plateau, parent, "Bed");
    lv_obj_set_size(ctx->plateau.racine, TUILE_LARGEUR, TUILE_HAUTEUR);
    lv_obj_set_pos(ctx->plateau.racine, MARGE + TUILE_LARGEUR + MARGE, TUILE_Y);

    ctx->fichier = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->fichier, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->fichier, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    /* Points de suspension plutot que deborder du cadre sur un nom de
     * fichier long (brief) : LV_LABEL_LONG_DOT exige une largeur explicite,
     * pas LV_SIZE_CONTENT, sinon il n'a rien contre quoi tronquer. */
    lv_label_set_long_mode(ctx->fichier, LV_LABEL_LONG_DOT);
    lv_obj_set_size(ctx->fichier, LARGEUR_CONTENU - 2 * MARGE, FICHIER_HAUTEUR);
    lv_label_set_text(ctx->fichier, "");
    lv_obj_set_pos(ctx->fichier, MARGE, FICHIER_Y);

    progression_creer(&ctx->progression, parent);
    lv_obj_set_size(ctx->progression.racine, PROGRESSION_LARGEUR, PROGRESSION_HAUTEUR);
    lv_obj_set_pos(ctx->progression.racine, MARGE, PROGRESSION_Y);

    /* Enfant de `parent` (l'ecran), PAS de ctx->progression.racine : le
     * contrat de progression.h limite l'ecran appelant a lire/dimensionner
     * `racine` et a passer par les fonctions publiques pour le reste
     * (progression_definir/_griser) -- devenir un enfant LVGL de son arbre
     * interne sort de ce contrat (revue tache 6, fix round 1, M-parent).
     * lv_obj_align_to() positionne ce libelle relativement a
     * ctx->progression.racine sans jamais s'y attacher : c'est exactement
     * la lecture de position/taille que progression.h autorise, rien de
     * plus. */
    ctx->temps = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->temps, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->temps, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->temps, "");
    lv_obj_align_to(ctx->temps, ctx->progression.racine, LV_ALIGN_CENTER, TEMPS_DECALAGE_X, 0);

    ctx->bouton_pause = bouton_creer(parent, "Pause", COULEUR_BOUTON, MARGE);
    /* Seul enfant du bouton (voir bouton_creer()) : conservé pour que
     * mettre_a_jour() puisse faire basculer "Pause"/"Resume" sans reparcourir
     * l'arbre a chaque appel. */
    ctx->label_pause = lv_obj_get_child(ctx->bouton_pause, 0);
    lv_obj_add_event_cb(ctx->bouton_pause, bouton_pause_cb, LV_EVENT_CLICKED, ctx);

    ctx->bouton_annuler =
        bouton_creer(parent, "Cancel", COULEUR_BOUTON, MARGE + BOUTON_LARGEUR + BOUTON_ECART);
    lv_obj_add_event_cb(ctx->bouton_annuler, bouton_annuler_cb, LV_EVENT_CLICKED, ctx);

    ctx->bouton_urgence = bouton_creer(parent, "E-STOP", COULEUR_BOUTON_URGENCE,
                                        MARGE + 2 * (BOUTON_LARGEUR + BOUTON_ECART));
    lv_obj_add_event_cb(ctx->bouton_urgence, bouton_urgence_cb, LV_EVENT_CLICKED, ctx);

    /* Tache 6 : bouton_creer() pose sa position sur BOUTONS_Y en dur --
     * repositionne/redimensionne explicitement ici pour la nouvelle rangee,
     * plutot que d'ajouter un parametre position a une fonction partagee par
     * trois boutons qui, eux, n'en ont jamais besoin. */
    ctx->bouton_macros = bouton_creer(parent, "Macros", COULEUR_BOUTON, MARGE);
    lv_obj_set_size(ctx->bouton_macros, RANGEE2_BOUTON_LARGEUR, MACROS_BOUTON_HAUTEUR);
    lv_obj_set_pos(ctx->bouton_macros, MARGE, MACROS_BOUTON_Y);
    lv_obj_add_event_cb(ctx->bouton_macros, bouton_macros_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_flag(ctx->bouton_macros, LV_OBJ_FLAG_HIDDEN); /* visible seulement si nb_macros > 0, voir mettre_a_jour() */

    /* Tache 5 : moitie droite de la meme rangee -- toujours visible
     * (contrairement a Macros juste au-dessus), voir le commentaire de
     * ecran_accueil.h sur bouton_reglage_fin pour pourquoi aucun grisage/
     * masquage n'est necessaire ici. */
    ctx->bouton_reglage_fin = bouton_creer(parent, "Fine Tune", COULEUR_BOUTON, FINE_TUNE_BOUTON_X);
    lv_obj_set_size(ctx->bouton_reglage_fin, RANGEE2_BOUTON_LARGEUR, MACROS_BOUTON_HAUTEUR);
    lv_obj_set_pos(ctx->bouton_reglage_fin, FINE_TUNE_BOUTON_X, MACROS_BOUTON_Y);
    lv_obj_add_event_cb(ctx->bouton_reglage_fin, bouton_reglage_fin_cb, LV_EVENT_CLICKED, ctx);

    /* Feature "Miniatures gcode", tâche B : masquée par défaut -- rendue
     * visible par mettre_a_jour() seulement quand le store (miniature.h)
     * porte un PNG PRÊT (jamais un widget "actif" par défaut sur des données
     * qui n'existent pas encore, même principe que bouton_macros ci-dessus).
     * Position/taille posées ici en valeur de repli (le coin haut-gauche du
     * rectangle réservé, MINIATURE_X/MINIATURE_Y) ; mettre_a_jour() les
     * RECALCULE à chaque affichage réel pour centrer l'image (dimensions
     * réelles inconnues avant le premier décodage, voir son commentaire). */
    ctx->miniature_image = lv_image_create(parent);
    lv_obj_set_pos(ctx->miniature_image, MINIATURE_X, MINIATURE_Y);
    lv_obj_add_flag(ctx->miniature_image, LV_OBJ_FLAG_HIDDEN);
    ctx->miniature_generation = 0; /* jamais affichée -- 0 n'est jamais une generation reelle du store */
}

/* Feature "Miniatures gcode", tâche B : affiche/masque ctx->miniature_image
 * selon le store dédié miniature.h (HORS etat_klipper_t -- `e`/`etat` ne
 * porte donc rien à ce sujet, contrairement au reste de mettre_a_jour()).
 *
 * Ordre STRICT à l'intérieur de cette fonction, jamais à inverser (voir le
 * contrat détaillé de miniature_lire()/miniature_purger() dans miniature.h,
 * §"transfert différé") : 1) lire l'instantané, 2) si une NOUVELLE
 * génération PRÊTE est disponible, ré-orienter le widget vers son pointeur
 * (ou le masquer si l'état n'est pas PRÊT), 3) PURGER en tout dernier -- le
 * tampon qu'un dépôt producteur a pu retirer depuis le dernier appel n'est
 * JAMAIS celui que cette fonction vient elle-même de poser sur le widget
 * dans CE cycle (il a forcément été remplacé par un dépôt ANTÉRIEUR), donc
 * purger après avoir ré-orienté le widget est toujours sûr.
 *
 * API LVGL 9.2 exacte pour une source PNG en mémoire (voir
 * simulateur/lvgl/src/libs/lodepng/lv_lodepng.c, decoder_info()/decoder_open()
 * -- vendorisé dans ce dépôt) : un `lv_image_dsc_t` dont `header.magic ==
 * LV_IMAGE_HEADER_MAGIC` (c'est CE bit, pas `header.cf`, qui fait classer la
 * source comme LV_IMAGE_SRC_VARIABLE par lv_image_src_get_type() -- un
 * `lv_image_dsc_t*` est reconnu par son PREMIER octet, qui doit être < 0x20,
 * voir lv_draw_image.c) et `data`/`data_size` = les octets PNG bruts. Le
 * décodeur LODEPNG (enregistré une fois pour toutes par lv_lodepng_init(),
 * voir app_main.c/host-test main.c) lit lui-même la signature PNG et les
 * dimensions réelles depuis `data` -- `header.w`/`header.h`/`header.cf` de CE
 * descripteur ne sont QUE des valeurs de repli, jamais utilisées par ce
 * chemin de décodage précis, donc laissées à 0/LV_COLOR_FORMAT_RAW ici. */
static void mettre_a_jour_miniature(ecran_accueil_ctx_t *ctx)
{
    miniature_instantane_t snap;
    miniature_lire(&snap);

    if (snap.etat == MINIATURE_PRETE && snap.donnees != NULL &&
        snap.generation != ctx->miniature_generation) {
        ctx->miniature_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        ctx->miniature_dsc.header.cf = LV_COLOR_FORMAT_RAW; /* ignoré par LODEPNG, voir ci-dessus */
        ctx->miniature_dsc.header.flags = 0;
        ctx->miniature_dsc.header.w = 0;
        ctx->miniature_dsc.header.h = 0;
        ctx->miniature_dsc.header.stride = 0;
        ctx->miniature_dsc.data = snap.donnees;
        ctx->miniature_dsc.data_size = (uint32_t)snap.taille;

        /* Interroge le décodeur pour les dimensions RÉELLES (lit seulement
         * la signature + l'en-tête IHDR, pas un décodage pixel complet --
         * voir decoder_info() dans lv_lodepng.c) AVANT de poser quoi que ce
         * soit sur le widget : sans ça, `snap.largeur`/`snap.hauteur` (issus
         * de la métadonnée Moonraker, potentiellement optimistes ou
         * absents pour ce chemin) ne garantiraient pas que l'image dessinée
         * tienne dans le rectangle réservé -- le calcul de zoom ci-dessous a
         * besoin des VRAIES dimensions pour garantir cette borne. Si le
         * décodeur échoue ici (PNG corrompu au-delà de sa seule signature,
         * déjà vérifiée par miniature.c mais jamais décodé en profondeur à
         * ce stade), l'image reste masquée -- jamais de crash, jamais un
         * lv_image_set_src() sur une source dont on ne connaît pas la
         * taille réelle. */
        lv_image_header_t header;
        if (lv_image_decoder_get_info(&ctx->miniature_dsc, &header) == LV_RESULT_OK &&
            header.w > 0 && header.h > 0) {
            lv_image_set_src(ctx->miniature_image, &ctx->miniature_dsc);

            /* Zoom "contain" calculé à la main (pas de mode d'ajustement
             * proportionnel automatique disponible sur cette version de
             * lv_image, voir lv_image_set_inner_align() dans lv_image.h --
             * seul LV_IMAGE_ALIGN_STRETCH déforme l'image pour remplir tout
             * le rectangle, jamais ce qu'on veut ici) : le plus petit des
             * deux facteurs horizontal/vertical, jamais l'inverse, pour
             * GARANTIR que l'image dessinée tient dans MINIATURE_LARGEUR x
             * MINIATURE_HAUTEUR quelle que soit la forme réelle du PNG (300x300
             * carré, ou un thumbnail plus rectangulaire). 256 = LV_ZOOM_IMAGE_NONE
             * (aucun zoom), voir lv_image_set_scale(). */
            uint32_t zoom_w = (uint32_t)MINIATURE_LARGEUR * 256u / (uint32_t)header.w;
            uint32_t zoom_h = (uint32_t)MINIATURE_HAUTEUR * 256u / (uint32_t)header.h;
            uint32_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
            lv_image_set_scale(ctx->miniature_image, zoom);

            /* Taille/position du widget recalculées à partir du zoom retenu
             * -- CE calcul, pas la simple confiance en LVGL pour clipper une
             * image plus grande que l'objet, est ce qui garantit
             * mécaniquement le non-recouvrement prouvé par les
             * _Static_assert sur MINIATURE_X/MINIATURE_Y plus haut : la
             * zone RÉELLEMENT dessinée (largeur_dessinee x hauteur_dessinee)
             * ne dépasse jamais MINIATURE_LARGEUR x MINIATURE_HAUTEUR par
             * construction (zoom = MIN des deux facteurs), donc son
             * placement centré reste toujours À L'INTÉRIEUR du rectangle
             * réservé. */
            int32_t largeur_dessinee = (int32_t)((uint32_t)header.w * zoom / 256u);
            int32_t hauteur_dessinee = (int32_t)((uint32_t)header.h * zoom / 256u);
            lv_obj_set_size(ctx->miniature_image, largeur_dessinee, hauteur_dessinee);
            lv_obj_set_pos(ctx->miniature_image,
                           MINIATURE_X + (MINIATURE_LARGEUR - largeur_dessinee) / 2,
                           MINIATURE_Y + (MINIATURE_HAUTEUR - hauteur_dessinee) / 2);
            lv_obj_clear_flag(ctx->miniature_image, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->miniature_image, LV_OBJ_FLAG_HIDDEN);
        }
        ctx->miniature_generation = snap.generation;
    } else if (snap.etat != MINIATURE_PRETE) {
        /* Systematique a chaque appel, meme discipline que le reste de ce
         * fichier (bouton_macros, grisage...) : ABSENTE/EN_COURS/ECHEC
         * masque TOUJOURS, y compris apres plusieurs allers-retours. */
        lv_obj_add_flag(ctx->miniature_image, LV_OBJ_FLAG_HIDDEN);
    }
    /* Sinon (PRETE mais generation deja affichee) : rien a refaire, le
     * widget reste tel quel -- pas de decodage/redimensionnement inutile a
     * chaque cycle de rafraichissement pour une image qui n'a pas change. */

    /* TOUJOURS en dernier -- voir le commentaire de tete de cette fonction
     * et miniature_purger() dans miniature.h. */
    miniature_purger();
}

static void ecran_accueil_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_accueil_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    char valeur[16];
    char consigne[16];
    char temps[16];

    /* Palier 1 tete (voir spec jalon 3 §6) : l'accueil affiche l'extrudeur 0
     * quel que soit nb_extrudeurs -- les grilles multi-tetes arrivent avec
     * les taches qui les consomment reellement, pas ici. */
    ui_format_temperature(valeur, sizeof(valeur), e->extrudeurs[0].actuelle);
    ui_format_temperature(consigne, sizeof(consigne), e->extrudeurs[0].consigne);
    tuile_definir_valeur(&ctx->buse, valeur);
    tuile_definir_consigne(&ctx->buse, consigne);

    ui_format_temperature(valeur, sizeof(valeur), e->plateau.actuelle);
    ui_format_temperature(consigne, sizeof(consigne), e->plateau.consigne);
    tuile_definir_valeur(&ctx->plateau, valeur);
    tuile_definir_consigne(&ctx->plateau, consigne);

    /* Copie bornee EXPLICITEMENT avant de tendre le texte a LVGL : le champ
     * fichier peut occuper la totalite de KLIPPER_FICHIER_MAX sans octet nul
     * (etat_klipper_t est un POD a champs fixes, voir etat_klipper.h) -- le
     * passer directement a lv_label_set_text() (qui appelle strlen()) lirait
     * au-dela du tampon. */
    char nom[KLIPPER_FICHIER_MAX + 1];
    memcpy(nom, e->fichier, KLIPPER_FICHIER_MAX);
    nom[KLIPPER_FICHIER_MAX] = '\0';
    lv_label_set_text(ctx->fichier, nom);

    progression_definir(&ctx->progression, e->progression);
    ui_format_duree(temps, sizeof(temps), e->temps_restant_s);
    lv_label_set_text(ctx->temps, temps);

    /* Tache 9 : bascule Pause/Resume selon l'etat REEL rapporte par le
     * backend, jamais un etat que le clic aurait anticipe -- ctx->en_pause
     * est relu par bouton_pause_cb() pour decider QUELLE action envoyer au
     * prochain clic. Une seule tuile de bouton, deux libelles, comme
     * l'exige le brief. */
    ctx->en_pause = e->impression_en_pause;
    lv_label_set_text(ctx->label_pause, ctx->en_pause ? "Resume" : "Pause");

    /* Grisage systematique a chaque appel, jamais incremental (meme lecon
     * que tuile_griser()/progression_griser(), voir la revue de la tache 4) :
     * un appel avec donnees_perimees=false doit rendre exactement les
     * couleurs normales, y compris apres plusieurs allers-retours. */
    tuile_griser(&ctx->buse, donnees_perimees);
    tuile_griser(&ctx->plateau, donnees_perimees);
    progression_griser(&ctx->progression, donnees_perimees);
    uint32_t couleur_texte = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    lv_obj_set_style_text_color(ctx->fichier, lv_color_hex(couleur_texte), 0);
    lv_obj_set_style_text_color(ctx->temps, lv_color_hex(couleur_texte), 0);

    /* Tache 9 (revue tache 6) : toute la rangee de boutons -- pas seulement
     * E-STOP -- prend LV_STATE_DISABLED sur donnees_perimees. Un bouton qui
     * s'enfonce visiblement et ne fait rien sur une liaison morte est un
     * mensonge d'interface ; §5.3 interdit le TEXTE d'erreur sur l'ecran, pas
     * l'etat desactive. Systematique a chaque appel, comme le grisage
     * ci-dessus : donnees_perimees=false doit toujours rendre les trois
     * boutons reactifs, y compris apres plusieurs allers-retours. ctx->
     * donnees_perimees est relu par les trois rappels de clic (voir plus
     * haut) pour rester honnete meme sous un evenement envoye directement
     * (lv_obj_send_event(), qui ne passe jamais par la verification tactile
     * de lv_indev.c sur ce meme etat). */
    ctx->donnees_perimees = donnees_perimees;
    bouton_definir_desactive(ctx->bouton_pause, donnees_perimees);
    bouton_definir_desactive(ctx->bouton_annuler, donnees_perimees);
    bouton_definir_desactive(ctx->bouton_urgence, donnees_perimees);

    /* Tache 6 : "jamais un bouton mort" (spec §3b, meme regle deja retenue
     * pour le futur bouton Imprimer) -- visible SEULEMENT si la machine a
     * annonce au moins une macro. Re-evalue a CHAQUE appel (systematique,
     * meme discipline que le grisage ci-dessus) : une machine qui perd ses
     * macros (redemarrage Klipper avec une config differente, par exemple)
     * ne doit pas laisser ce bouton visible sur la foi d'un etat perime. */
    if (e->nb_macros > 0) {
        lv_obj_clear_flag(ctx->bouton_macros, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->bouton_macros, LV_OBJ_FLAG_HIDDEN);
    }

    mettre_a_jour_miniature(ctx);
}

const ecran_desc_t ECRAN_ACCUEIL = {
    .id = "accueil",
    .titre = "Home",
    .taille_contexte = sizeof(ecran_accueil_ctx_t),
    .construire = ecran_accueil_construire,
    .mettre_a_jour = ecran_accueil_mettre_a_jour,
    .detruire = NULL,
};
