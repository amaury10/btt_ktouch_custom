/* Implémentation de l'afficheur : voir afficheur.h pour le contrat. */

#include "afficheur.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* Le simulateur ne vaut que s'il rend ce que rend l'appareil. Côté ESP, LVGL
 * vient du registre en 9.2.2 (firmware/dependencies.lock) ; ici il vient du
 * sous-module simulateur/lvgl. Si l'un des deux bouge sans l'autre, le
 * simulateur se met à mentir en silence — ce test transforme cette dérive en
 * erreur de compilation. Mettre à jour les deux ensemble, jamais l'un seul. */
_Static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 2 && LVGL_VERSION_PATCH == 2,
               "LVGL du simulateur != 9.2.2 : verifier firmware/dependencies.lock");

/* Nombre de lignes du tampon de rendu partiel utilisé en mode hors écran.
 * 60 lignes de 800 pixels RGB565 : compromis arbitraire entre le nombre
 * d'appels au rappel de vidage et la mémoire consommée par le tampon
 * intermédiaire (le cadre complet, lui, fait 800*480*2 octets et vit à
 * part). */
#define LIGNES_TAMPON_PARTIEL 60

static afficheur_mode_t mode_courant;
static lv_display_t *afficheur_disp;
static uint8_t tampon_rendu[AFFICHEUR_LARGEUR * LIGNES_TAMPON_PARTIEL * 2];
/* Cadre complet hors écran, au même format que ce que le panneau reçoit :
 * RGB565, 800x480. C'est dans ce tampon que la capture PNG lit ses pixels. */
static uint8_t cadre_hors_ecran[AFFICHEUR_LARGEUR * AFFICHEUR_HAUTEUR * 2];

/* Rappel de vidage du mode hors écran : recopie la zone rendue par LVGL
 * (px_map, tampon partiel) dans le cadre complet détenu par ce module.
 *
 * Le pas source est la largeur de LA ZONE (lv_area_get_width), pas 800 : une
 * zone partielle peut être plus étroite que l'écran entier (bord droit d'un
 * widget, par exemple). Le pas destination, lui, est toujours 800 puisque le
 * cadre complet a la largeur de l'écran. Confondre les deux pas produit une
 * image décalée en biais à chaque ligne — un défaut discret qui ressemble à
 * une erreur de mise en page plutôt qu'à un bug de copie. */
static void vidage_hors_ecran(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t largeur_zone = lv_area_get_width(area);
    int32_t hauteur_zone = lv_area_get_height(area);
    size_t pas_source = (size_t)largeur_zone * 2;
    size_t pas_dest = (size_t)AFFICHEUR_LARGEUR * 2;

    for (int32_t ligne = 0; ligne < hauteur_zone; ligne++) {
        size_t decalage_dest = (size_t)(area->y1 + ligne) * pas_dest + (size_t)area->x1 * 2;
        memcpy(cadre_hors_ecran + decalage_dest, px_map + (size_t)ligne * pas_source, pas_source);
    }

    lv_display_flush_ready(disp);
}

bool afficheur_demarrer(afficheur_mode_t mode)
{
    lv_init();
    mode_courant = mode;

    if (mode == AFFICHEUR_FENETRE) {
        afficheur_disp = lv_sdl_window_create(AFFICHEUR_LARGEUR, AFFICHEUR_HAUTEUR);
        if (afficheur_disp == NULL) {
            return false;
        }
        /* Le pointeur souris SDL EST notre tactile en simulation : sans lui,
         * aucun bouton de l'interface ne réagirait aux clics. */
        lv_sdl_mouse_create();
        return true;
    }

    afficheur_disp = lv_display_create(AFFICHEUR_LARGEUR, AFFICHEUR_HAUTEUR);
    if (afficheur_disp == NULL) {
        return false;
    }
    lv_display_set_color_format(afficheur_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(afficheur_disp, tampon_rendu, NULL, sizeof(tampon_rendu),
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(afficheur_disp, vidage_hors_ecran);
    memset(cadre_hors_ecran, 0, sizeof(cadre_hors_ecran));
    return true;
}

void afficheur_pomper(uint32_t ms)
{
    /* Ordre imposé : lv_tick_inc() d'abord, lv_timer_handler() ensuite. Les
     * animations et temporisations LVGL lisent l'horloge au moment où elles
     * sont traitées ; les appeler dans l'autre sens leur ferait traiter un
     * temps qui n'a pas encore avancé. */
    lv_tick_inc(ms);
    lv_timer_handler();
}

bool afficheur_capturer(const char *chemin_png)
{
    if (mode_courant != AFFICHEUR_HORS_ECRAN) {
        /* En mode FENETRE, les pixels vivent dans la texture SDL, pas dans
         * notre cadre : rien ici à écrire en PNG. */
        return false;
    }

    /* RGB565 -> RGB888, avec réplication des bits de poids fort. Un simple
     * décalage (r5 << 3) laisserait les 3 bits de poids faible à zéro : le
     * blanc RGB565 (0x1F composant) deviendrait 0xF8 au lieu de 0xFF. La
     * réplication (r5 << 3) | (r5 >> 2) recopie les bits hauts dans les bits
     * bas, comme le ferait un vrai convertisseur RGB565 -> RGB888. */
    static uint8_t rgb888[AFFICHEUR_LARGEUR * AFFICHEUR_HAUTEUR * 3];
    for (size_t i = 0; i < (size_t)AFFICHEUR_LARGEUR * AFFICHEUR_HAUTEUR; i++) {
        uint16_t pixel = (uint16_t)(cadre_hors_ecran[i * 2] | (cadre_hors_ecran[i * 2 + 1] << 8));
        uint8_t r5 = (uint8_t)((pixel >> 11) & 0x1F);
        uint8_t g6 = (uint8_t)((pixel >> 5) & 0x3F);
        uint8_t b5 = (uint8_t)(pixel & 0x1F);
        rgb888[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        rgb888[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        rgb888[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }

    int ok = stbi_write_png(chemin_png, AFFICHEUR_LARGEUR, AFFICHEUR_HAUTEUR, 3, rgb888,
                             AFFICHEUR_LARGEUR * 3);
    return ok != 0;
}

void afficheur_arreter(void)
{
    if (afficheur_disp != NULL) {
        lv_display_delete(afficheur_disp);
        afficheur_disp = NULL;
    }
    lv_deinit();
}
