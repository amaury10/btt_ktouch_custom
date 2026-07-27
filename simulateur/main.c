/* Point d'entrée provisoire du simulateur : dessine une mire de vérification
 * et soit la capture en PNG (mode hors écran), soit l'affiche dans une
 * fenêtre SDL interactive. Ce fichier sera remplacé par le vrai assemblage
 * d'écrans dans une tâche ultérieure du jalon 2b ; son seul but ici est de
 * prouver que l'afficheur (fond, les quatre polices Montserrat 14/20/28/48,
 * un widget, cadrage) rend correctement dans les deux modes. */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "afficheur.h"
#include "lvgl.h"

/* Taille des quatre carrés témoins de coin, en pixels. */
#define TAILLE_CARRE_COIN 24

static void dessiner_mire(void)
{
    lv_obj_t *ecran = lv_screen_active();
    lv_obj_set_style_bg_color(ecran, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(ecran, LV_OPA_COVER, 0);

    lv_obj_t *titre = lv_label_create(ecran);
    lv_obj_set_style_text_font(titre, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(titre, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(titre, "K-Touch simulator - test pattern");
    lv_obj_align(titre, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *ligne48 = lv_label_create(ecran);
    lv_obj_set_style_text_font(ligne48, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ligne48, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(ligne48, "Hello K-Touch");
    lv_obj_align(ligne48, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *barre = lv_bar_create(ecran);
    lv_obj_set_size(barre, 400, 24);
    lv_obj_align(barre, LV_ALIGN_CENTER, 0, 60);
    lv_bar_set_value(barre, 42, LV_ANIM_OFF);

    /* Montserrat 14 et 20 sont activées dans lv_conf.h au même titre que 28
     * et 48, mais rien ne les faisait apparaître dans cette mire : une
     * mauvaise valeur pour l'une d'elles serait restée invisible jusqu'à
     * ce qu'un écran ultérieur en ait besoin. Ces deux lignes ferment
     * l'écart entre ce que le commentaire d'en-tête de ce fichier annonce
     * (« quatre polices ») et ce que la capture prouvait réellement. */
    lv_obj_t *ligne20 = lv_label_create(ecran);
    lv_obj_set_style_text_font(ligne20, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ligne20, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(ligne20, "Montserrat 20 - the quick brown fox");
    lv_obj_align(ligne20, LV_ALIGN_CENTER, 0, 110);

    lv_obj_t *ligne14 = lv_label_create(ecran);
    lv_obj_set_style_text_font(ligne14, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ligne14, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(ligne14, "Montserrat 14 - the quick brown fox");
    lv_obj_align(ligne14, LV_ALIGN_CENTER, 0, 140);

    /* Quatre carrés de coin : la moindre confusion de pas dans le rappel de
     * vidage hors écran (voir afficheur.c) les déplace ou les déforme, ce
     * qui les rend le témoin le plus simple à vérifier à l'œil. */
    const lv_align_t coins[4] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT,
                                  LV_ALIGN_BOTTOM_RIGHT};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *carre = lv_obj_create(ecran);
        lv_obj_set_size(carre, TAILLE_CARRE_COIN, TAILLE_CARRE_COIN);
        lv_obj_set_style_bg_color(carre, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(carre, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(carre, 0, 0);
        lv_obj_set_style_radius(carre, 0, 0);
        lv_obj_align(carre, coins[i], 0, 0);
    }
}

int main(int argc, char **argv)
{
    const char *chemin_capture = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            chemin_capture = argv[i + 1];
            i++;
        }
    }

    afficheur_mode_t mode = (chemin_capture != NULL) ? AFFICHEUR_HORS_ECRAN : AFFICHEUR_FENETRE;
    if (!afficheur_demarrer(mode)) {
        fprintf(stderr, "echec du demarrage de l'afficheur (mode %s)\n",
                mode == AFFICHEUR_FENETRE ? "fenetre" : "hors ecran");
        return 1;
    }

    dessiner_mire();

    if (chemin_capture != NULL) {
        /* Un cycle de pompe suffit à laisser LVGL rendre l'écran une
         * première fois avant la capture (délai nul : aucune animation
         * n'est en jeu ici). */
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
     * fenêtre par le gestionnaire de fenêtres). */
    for (;;) {
        afficheur_pomper(16);
        usleep(16 * 1000);
    }
}
