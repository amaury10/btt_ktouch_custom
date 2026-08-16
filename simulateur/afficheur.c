/* Implémentation de l'afficheur : voir afficheur.h pour le contrat. */

#include "afficheur.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include <SDL2/SDL.h>

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

/* true entre un afficheur_demarrer() qui a réussi et l'afficheur_arreter()
 * qui lui correspond. Fait de afficheur_demarrer() sur un afficheur déjà
 * démarré une erreur (rend false, ne touche pas à celui qui tourne) plutôt
 * que d'écraser silencieusement afficheur_disp et de fuir la fenêtre SDL,
 * le renderer et la texture que créait l'appel précédent. */
static bool demarre;
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
    if (demarre) {
        /* Contrat choisi (voir afficheur.h) : ne jamais remplacer
         * silencieusement un afficheur en cours. */
        return false;
    }

    /* lv_init() est sûr à appeler même après une tentative précédente
     * restée incomplète (avortée plus bas par un `return false`) : LVGL le
     * documente lui-même comme sans effet si déjà initialisé (un seul
     * WARN loggué), donc ce module n'a pas besoin de suivre un état
     * intermédiaire entre lv_init() et un afficheur_disp effectivement
     * créé. */
    lv_init();

    if (mode == AFFICHEUR_FENETRE) {
        /* lv_sdl_window_create() (vendorisé, simulateur/lvgl/src/drivers/
         * sdl/lv_sdl_window.c) n'examine AUCUN code de retour de
         * SDL_Init/SDL_CreateWindow/SDL_CreateRenderer/SDL_CreateTexture,
         * et ne rend NULL que sur un échec d'allocation mémoire. En tête
         * de processus (DISPLAY et WAYLAND_DISPLAY vides), il rend donc un
         * lv_display_t parfaitement valide qui dessine dans le vide —
         * vérifié empiriquement avant d'écrire ce contournement (voir
         * simulateur/README.md) : sans lui, cette fonction rendait true
         * dans exactement le cas où sa documentation promettait false.
         *
         * On appelle donc SDL_Init() nous-mêmes en premier et on vérifie
         * SON retour. Ça ne suffit PAS à soi seul : vérifié empiriquement
         * que sur cette machine, SDL2 enregistre aussi des pilotes vidéo
         * "dummy" et "offscreen" (SDL_GetVideoDriver() les liste à côté de
         * x11/wayland) et, quand x11 et wayland échouent faute de
         * DISPLAY/WAYLAND_DISPLAY, retombe silencieusement sur "offscreen"
         * — SDL_Init(SDL_INIT_VIDEO) rend alors 0 (succès) quand même. Il
         * faut donc aussi rejeter explicitement ces deux pilotes de repli,
         * qui n'affichent jamais rien à un utilisateur : c'est précisément
         * le cas que ce commentaire promet de détecter. lv_sdl_window_create
         * rappellera SDL_Init en interne juste après notre propre appel :
         * sans effet, SDL incrémente un compteur de référence par
         * sous-système plutôt que d'échouer sur un second appel.
         *
         * Limite assumée : ceci détecte l'échec du sous-système vidéo et
         * son repli sur un pilote sans affichage réel connu de cette
         * machine (dummy, offscreen). Un SDL_CreateWindow/Renderer/Texture
         * qui échouerait avec un VRAI pilote (x11, wayland, ...) resterait
         * invisible, pour la même raison que lv_sdl_window_create ne le
         * détecte pas non plus — aucune vérification supplémentaire
         * n'existe côté LVGL sur laquelle s'appuyer. */
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            SDL_Quit();
            return false;
        }
        const char *pilote_sdl = SDL_GetCurrentVideoDriver();
        if (pilote_sdl != NULL &&
            (strcmp(pilote_sdl, "dummy") == 0 || strcmp(pilote_sdl, "offscreen") == 0)) {
            SDL_Quit();
            return false;
        }

        afficheur_disp = lv_sdl_window_create(AFFICHEUR_LARGEUR, AFFICHEUR_HAUTEUR);
        if (afficheur_disp == NULL) {
            SDL_Quit();
            return false;
        }
        /* Le pointeur souris SDL EST notre tactile en simulation : sans lui,
         * aucun bouton de l'interface ne réagirait aux clics. */
        lv_sdl_mouse_create();
        mode_courant = mode;
        demarre = true;
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
    mode_courant = mode;
    demarre = true;
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
    if (!demarre) {
        /* Rien à faire : jamais démarré, ou déjà arrêté. Sûr à appeler
         * plusieurs fois de suite (par exemple depuis un gestionnaire de
         * sortie qui ne sait pas dans quel état le module se trouve). */
        return;
    }

    afficheur_mode_t mode_arrete = mode_courant;

    if (afficheur_disp != NULL) {
        /* Supprimer l'affichage AVANT de fermer SDL : en mode FENETRE, la
         * suppression déclenche release_disp_cb (lv_sdl_window.c), qui
         * détruit fenêtre/renderer/texture SDL — ces appels ont besoin que
         * SDL soit encore initialisé pour être valides. */
        lv_display_delete(afficheur_disp);
        afficheur_disp = NULL;
    }

    if (mode_arrete == AFFICHEUR_FENETRE) {
        /* lv_sdl_quit() (simulateur/lvgl/src/drivers/sdl/lv_sdl_window.c)
         * appelle SDL_Quit() et supprime le timer d'événements créé par
         * lv_sdl_window_create() lors du démarrage. Sans cet appel, un
         * cycle démarrer/arrêter/démarrer répété fuirait ce timer à chaque
         * cycle, et le sous-système vidéo SDL resterait initialisé même
         * après un arrêt complet. */
        lv_sdl_quit();
    }

    lv_deinit();
    demarre = false;
}
