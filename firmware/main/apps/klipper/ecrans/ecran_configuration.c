/* Implémentation : voir ecran_configuration.h pour le contrat.
 *
 * Mise en page (800x436, sous la barre d'état construite par habillage.c) :
 * un champ adresse (label + bouton "Edit" qui ouvre le clavier), un
 * sélecteur de type de machine (une seule entrée aujourd'hui), un bouton
 * "Save" plein largeur en bas. Toutes les positions dérivent de macros pour
 * la même raison que ecran_accueil.c : un futur ajustement de l'une d'entre
 * elles ne doit pas désaligner silencieusement le reste. */
#include "ecran_configuration.h"

#include <stdio.h>
#include <string.h>

#include "habillage.h"
#include "hote_parse.h"
#include "navigation.h"
#include "source_reglages.h"

#define LARGEUR_CONTENU 800
#define HAUTEUR_CONTENU 436

#define MARGE 24

#define TITRE_Y1       20
#define TITRE_HAUTEUR   26

#define VALEUR_Y      (TITRE_Y1 + TITRE_HAUTEUR + 8)
#define VALEUR_HAUTEUR  44
#define VALEUR_LARGEUR 500

#define BOUTON_MODIFIER_LARGEUR 140
#define BOUTON_PETIT_HAUTEUR     48
#define ECART_MODIFIER           20

#define TITRE_Y2 (VALEUR_Y + VALEUR_HAUTEUR + 30)

#define DROPDOWN_Y        (TITRE_Y2 + TITRE_HAUTEUR + 8)
#define DROPDOWN_LARGEUR   360
#define DROPDOWN_HAUTEUR    48

#define BOUTON_ENREGISTRER_LARGEUR  220
#define BOUTON_ENREGISTRER_HAUTEUR   70
/* Position littérale, PAS dérivée de "HAUTEUR_CONTENU - MARGE - HAUTEUR"
 * (revue tâche 8, round 1, Q9) : cette dérivation plaçait le bouton pile en
 * bas du contenu, exactement là où le bandeau de notification de l'habillage
 * (60 px, superposé en ABSOLU depuis le bas de l'ÉCRAN ENTIER, pas de la
 * zone de contenu -- voir BANDEAU_HAUTEUR/construire_bandeau() dans
 * habillage.c) vient recouvrir. Sur le chemin d'erreur -- exactement celui
 * qu'un clic sur CE bouton peut déclencher -- le bandeau rouge masquait donc
 * la moitié basse du bouton qui vient de produire l'erreur. Valeur choisie
 * avec une marge confortable sous le sélecteur et largement au-dessus de la
 * bande de recouvrement du bandeau (vérifié par les _Static_assert
 * ci-dessous, qui encodent le calcul en coordonnées ABSOLUES d'écran -- la
 * même erreur qu'une dérivation automatique aurait reproduite en silence si
 * HAUTEUR_CONTENU changeait un jour). */
#define BOUTON_ENREGISTRER_Y 280

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_PLACEHOLDER      0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Bande couverte par le bandeau de notification de l'habillage, en
 * coordonnées ABSOLUES d'écran (voir habillage.c : BARRE_HAUTEUR = 44,
 * BANDEAU_HAUTEUR = 60, sur un écran de HAUTEUR_ECRAN = 480 -- ces trois
 * valeurs DOIVENT rester synchronisées avec habillage.c, aucune des deux
 * n'incluant l'autre). Ce fichier vit dans la zone de CONTENU (sous la barre
 * d'état, à l'intérieur de g_contenu) : une position ici en coordonnées
 * locales `y` correspond donc à l'absolu `BARRE_HAUTEUR_ECRAN + y`. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

/* Verifie une fois, a la compilation, que la rangee champ+bouton et le
 * selecteur ne debordent pas de la largeur du contenu, et que les rangees ne
 * se chevauchent pas verticalement -- meme discipline que les _Static_assert
 * de ecran_accueil.c (revue tache 6). */
_Static_assert(MARGE + VALEUR_LARGEUR + ECART_MODIFIER + BOUTON_MODIFIER_LARGEUR + MARGE <= LARGEUR_CONTENU,
                "le champ adresse + bouton Edit debordent de la largeur du contenu");
_Static_assert(MARGE + DROPDOWN_LARGEUR + MARGE <= LARGEUR_CONTENU,
                "le selecteur de machine deborde de la largeur du contenu");
_Static_assert(DROPDOWN_Y + DROPDOWN_HAUTEUR <= BOUTON_ENREGISTRER_Y,
                "le selecteur de machine chevauche le bouton Save");
_Static_assert(BOUTON_ENREGISTRER_Y + BOUTON_ENREGISTRER_HAUTEUR <= HAUTEUR_CONTENU,
                "le bouton Save deborde de la hauteur du contenu");
/* Le garde-fou qui manquait avant la revue de la tache 8, round 1, Q9 :
 * le bas du bouton Save (en coordonnees ABSOLUES d'ecran) doit rester
 * strictement au-dessus du haut du bandeau de notification -- sans quoi le
 * bandeau d'ERREUR que ce bouton peut lui-meme declencher recouvrirait le
 * bouton qui vient de le produire. */
_Static_assert(BARRE_HAUTEUR_ECRAN + BOUTON_ENREGISTRER_Y + BOUTON_ENREGISTRER_HAUTEUR <= BANDEAU_Y_ECRAN,
                "le bouton Save chevauche la bande du bandeau de notification de l'habillage");

/* --- Validation pure -------------------------------------------------- */

static void ecrire_erreur(char *erreur, size_t taille_erreur, const char *message)
{
    if (erreur == NULL || taille_erreur == 0) {
        return;
    }
    snprintf(erreur, taille_erreur, "%s", message);
}

bool ecran_configuration_valider(const char *saisie, backend_hote_t *hote_sortie,
                                  char *erreur, size_t taille_erreur)
{
    if (saisie == NULL || saisie[0] == '\0') {
        ecrire_erreur(erreur, taille_erreur, "Printer address cannot be empty");
        return false;
    }

    /* CLAVIER_VALEUR_MAX (128, voir clavier.h) : la plus grande saisie que le
     * clavier tactile puisse jamais rendre ; +8 : au plus ":65535" (6
     * caracteres) et le NUL final, marge pour synthetiser "adresse:port par
     * defaut" ci-dessous. Cette fonction reste appelable avec une chaine plus
     * longue que CLAVIER_VALEUR_MAX (rien ne la lie au clavier), d'ou le
     * controle de troncature qui suit -- il ne depend pas de la provenance de
     * `saisie`. */
    char chaine[CLAVIER_VALEUR_MAX + 8];
    int longueur;
    if (strchr(saisie, ':') != NULL) {
        /* Deja "adresse:port" (ou litteral IPv6 -- hote_parse() decoupe sur
         * le DERNIER ':', voir hote_parse.h) : aucune synthese necessaire. */
        longueur = snprintf(chaine, sizeof(chaine), "%s", saisie);
    } else {
        /* Pas de port saisi : le port par defaut s'applique -- CONTRAIREMENT
         * a hote_parse() seul, qui juge une chaine sans ':' entierement
         * inexploitable (voir son commentaire de tete). C'est le cas normal
         * d'un utilisateur qui tape juste une adresse ("192.168.1.50") sans
         * jamais avoir a connaitre le port Moonraker par defaut. Synthetiser
         * "adresse:7125" et deleguer a hote_parse() plutot que de dupliquer
         * sa logique de bornes : une seule regle de validation d'adresse
         * dans tout le firmware. */
        longueur = snprintf(chaine, sizeof(chaine), "%s:%u", saisie, (unsigned)HOTE_PARSE_PORT_DEFAUT);
    }
    if (longueur < 0 || (size_t)longueur >= sizeof(chaine)) {
        ecrire_erreur(erreur, taille_erreur, "Printer address is too long");
        return false;
    }

    backend_hote_t hote;
    if (!hote_parse(chaine, &hote)) {
        ecrire_erreur(erreur, taille_erreur, "Printer address is not valid");
        return false;
    }

    if (hote_sortie != NULL) {
        *hote_sortie = hote;
    }
    return true;
}

/* --- Widgets ------------------------------------------------------------ */

static void ecran_configuration_rafraichir_label(ecran_configuration_ctx_t *ctx)
{
    if (ctx->saisie[0] != '\0') {
        lv_label_set_text(ctx->valeur_label, ctx->saisie);
        lv_obj_set_style_text_color(ctx->valeur_label, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    } else {
        lv_label_set_text(ctx->valeur_label, "Not configured");
        lv_obj_set_style_text_color(ctx->valeur_label, lv_color_hex(COULEUR_PLACEHOLDER), 0);
    }
}

static void ecran_configuration_rappel_clavier(const char *valeur, void *contexte)
{
    ecran_configuration_ctx_t *ctx = contexte;
    if (ctx == NULL || valeur == NULL) {
        return; /* annule : rien ne change (contrat clavier.h) */
    }
    /* `valeur` est deja bornee a CLAVIER_VALEUR_MAX-1 octets par clavier.c
     * (voir clavier.h) : ctx->saisie fait exactement CLAVIER_VALEUR_MAX
     * octets, la copie ne peut donc jamais deborder. */
    snprintf(ctx->saisie, sizeof(ctx->saisie), "%s", valeur);
    ecran_configuration_rafraichir_label(ctx);
}

static void ecran_configuration_bouton_modifier_cb(lv_event_t *e)
{
    ecran_configuration_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    clavier_ouvrir("Printer address", ctx->saisie, CLAVIER_TEXTE,
                    ecran_configuration_rappel_clavier, ctx);
}

static void ecran_configuration_bouton_enregistrer_cb(lv_event_t *e)
{
    ecran_configuration_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }

    backend_hote_t hote;
    char erreur[64];
    if (!ecran_configuration_valider(ctx->saisie, &hote, erreur, sizeof(erreur))) {
        /* Hote invalide : notifie et RESTE sur l'ecran (brief) -- rien
         * n'est enregistre, rien ne navigue. */
        habillage_notifier(erreur, true);
        return;
    }

    esp_err_t resultat = ui_reglages_definir_hote(&hote);
    if (resultat != ESP_OK) {
        /* Echec d'ecriture (NVS pleine, panne materielle -- voir
         * reglages_definir_hote()) : ne jamais annoncer "enregistre" pour ce
         * qui ne l'est pas, meme regle que le grisage de donnees perimees
         * (spec 5.3) applique a ce nouvel endroit. Non prevu explicitement
         * par le brief (qui ne considere que l'hote invalide comme cas
         * d'echec) mais la seule extension honnete de la meme regle. */
        habillage_notifier("Could not save settings", true);
        return;
    }

    habillage_notifier("Settings saved", false);
    navigation_accueil();
}

static void ecran_configuration_construire(lv_obj_t *parent, void *contexte)
{
    ecran_configuration_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Prerempli depuis l'hote deja enregistre, via la facade ui/source_reglages.h
     * (jamais core/reglages.h directement -- voir ecran_configuration.h et
     * source_reglages.h pour pourquoi). Chaine vide si jamais rien n'a ete
     * enregistre (premier demarrage). */
    backend_hote_t actuel;
    if (ui_reglages_hote(&actuel)) {
        snprintf(ctx->saisie, sizeof(ctx->saisie), "%s:%u", actuel.adresse, (unsigned)actuel.port);
    } else {
        ctx->saisie[0] = '\0';
    }

    lv_obj_t *titre_adresse = lv_label_create(parent);
    lv_obj_set_style_text_font(titre_adresse, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titre_adresse, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(titre_adresse, "Printer address");
    lv_obj_set_pos(titre_adresse, MARGE, TITRE_Y1);

    ctx->valeur_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->valeur_label, &lv_font_montserrat_20, 0);
    /* Points de suspension plutot que deborder sur une adresse trop longue
     * pour l'espace disponible (meme technique que le nom de fichier de
     * ecran_accueil.c) : LV_LABEL_LONG_DOT exige une largeur explicite. */
    lv_label_set_long_mode(ctx->valeur_label, LV_LABEL_LONG_DOT);
    lv_obj_set_size(ctx->valeur_label, VALEUR_LARGEUR, VALEUR_HAUTEUR);
    lv_obj_set_pos(ctx->valeur_label, MARGE, VALEUR_Y);
    /* LV_LABEL_LONG_DOT calcule sa troncature a partir de la largeur de
     * CONTENU resolue de l'objet (lv_obj_get_content_width()), pas de la
     * taille qu'on vient de lui donner : sans ce lv_obj_update_layout(), le
     * premier lv_label_set_text() ci-dessous voit une largeur pas encore
     * mise a jour (souvent proche de 0) et tronque en "..." meme un texte
     * court comme "Not configured" -- constate directement (RED de la tache
     * 8 : les trois premieres assertions de host-test/tests/
     * test_ecran_configuration.c lisaient "..." au lieu du texte attendu). */
    lv_obj_update_layout(ctx->valeur_label);
    ecran_configuration_rafraichir_label(ctx);

    ctx->bouton_modifier = lv_button_create(parent);
    lv_obj_set_size(ctx->bouton_modifier, BOUTON_MODIFIER_LARGEUR, BOUTON_PETIT_HAUTEUR);
    lv_obj_set_pos(ctx->bouton_modifier, LARGEUR_CONTENU - MARGE - BOUTON_MODIFIER_LARGEUR, VALEUR_Y);
    lv_obj_set_style_bg_color(ctx->bouton_modifier, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(ctx->bouton_modifier, 0, 0);
    lv_obj_set_style_shadow_width(ctx->bouton_modifier, 0, 0);
    lv_obj_set_style_radius(ctx->bouton_modifier, 8, 0);
    lv_obj_t *label_modifier = lv_label_create(ctx->bouton_modifier);
    lv_obj_set_style_text_color(label_modifier, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label_modifier, "Edit");
    lv_obj_center(label_modifier);
    lv_obj_add_event_cb(ctx->bouton_modifier, ecran_configuration_bouton_modifier_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *titre_type = lv_label_create(parent);
    lv_obj_set_style_text_font(titre_type, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titre_type, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(titre_type, "Machine type");
    lv_obj_set_pos(titre_type, MARGE, TITRE_Y2);

    /* Une seule entree ("Klipper / Moonraker") : le fork astro en ajoutera
     * une seconde -- pas d'entree factice ici pour "faire riche" (brief). */
    ctx->dropdown_type = lv_dropdown_create(parent);
    lv_dropdown_set_options(ctx->dropdown_type, "Klipper / Moonraker");
    lv_obj_set_size(ctx->dropdown_type, DROPDOWN_LARGEUR, DROPDOWN_HAUTEUR);
    lv_obj_set_pos(ctx->dropdown_type, MARGE, DROPDOWN_Y);

    ctx->bouton_enregistrer = lv_button_create(parent);
    lv_obj_set_size(ctx->bouton_enregistrer, BOUTON_ENREGISTRER_LARGEUR, BOUTON_ENREGISTRER_HAUTEUR);
    lv_obj_set_pos(ctx->bouton_enregistrer, (LARGEUR_CONTENU - BOUTON_ENREGISTRER_LARGEUR) / 2,
                    BOUTON_ENREGISTRER_Y);
    lv_obj_set_style_bg_color(ctx->bouton_enregistrer, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(ctx->bouton_enregistrer, 0, 0);
    lv_obj_set_style_shadow_width(ctx->bouton_enregistrer, 0, 0);
    lv_obj_set_style_radius(ctx->bouton_enregistrer, 10, 0);
    lv_obj_t *label_enregistrer = lv_label_create(ctx->bouton_enregistrer);
    lv_obj_set_style_text_font(label_enregistrer, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_enregistrer, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label_enregistrer, "Save");
    lv_obj_center(label_enregistrer);
    lv_obj_add_event_cb(ctx->bouton_enregistrer, ecran_configuration_bouton_enregistrer_cb, LV_EVENT_CLICKED, ctx);
}

const ecran_desc_t ECRAN_CONFIGURATION = {
    .id = "configuration",
    .titre = "Settings",
    .taille_contexte = sizeof(ecran_configuration_ctx_t),
    .construire = ecran_configuration_construire,
    .mettre_a_jour = NULL,  /* pas d'etat backend a suivre sur cet ecran */
    .detruire = NULL,       /* rien a liberer au-dela du contexte lui-meme */
};
