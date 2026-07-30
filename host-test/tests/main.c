#include <stdio.h>

#include "lvgl.h"

int tests_echoues = 0;
int tests_lances = 0;

void suite_harnais(void);
void suite_contrat(void);
void suite_moonraker_parse(void);
void suite_moonraker_rpc(void);
void suite_fixtures_moonraker(void);
void suite_moonraker_boite(void);
void suite_etat_store(void);
void suite_liaison(void);
void suite_backend_factice(void);
void suite_hote_parse(void);
void suite_boucle_cycle(void);
void suite_plateforme(void);
void suite_navigation(void);
void suite_habillage(void);
void suite_widgets(void);
void suite_ecran_accueil(void);
void suite_ecran_accueil_idle(void);
void suite_ecran_accueil_idle_jog(void);
void suite_ecran_accueil_idle_home(void);
void suite_ecran_accueil_idle_temp(void);
void suite_accueil_choix(void);
void suite_clavier(void);
void suite_ecran_configuration(void);
void suite_commandes(void);
void suite_jouet(void);
void suite_web_macros(void);
void suite_ecran_macros(void);
void suite_klipper_gcode(void);
void suite_klipper_paliers(void);
void suite_selecteur_pas(void);

/* Taille de l'afficheur hors écran utilisé par les tests LVGL : aucun pixel
 * n'y est jamais examiné (suite_navigation ne fait que compter des appels de
 * cycle de vie), donc 32x32 suffit largement — voir
 * simulateur/afficheur.c pour le mode hors écran "vrai", qui rend à la
 * résolution de l'appareil (800x480) pour une capture PNG. */
#define AFFICHAGE_TEST_LARGEUR 32
#define AFFICHAGE_TEST_HAUTEUR 32
static uint8_t g_tampon_affichage_test[AFFICHAGE_TEST_LARGEUR * AFFICHAGE_TEST_HAUTEUR * 2];

/* Rappel de vidage minimal : aucune destination réelle, juste signaler à
 * LVGL que la zone a été "affichée" pour qu'il ne bloque pas dessus. */
static void vidage_test(lv_display_t *disp, const lv_area_t *zone, uint8_t *tampon_pixels)
{
    (void)zone;
    (void)tampon_pixels;
    lv_display_flush_ready(disp);
}

/* Sans afficheur initialisé, lv_screen_active() rend NULL et tout appel LVGL
 * part en assertion (voir LV_USE_ASSERT_NULL dans simulateur/lv_conf.h) :
 * les tests de navigation ont besoin d'un écran actif réel, même s'il ne
 * dessine jamais rien à l'écran. Initialisation unique, faite ici plutôt que
 * dans chaque suite, pour qu'il n'y ait qu'un seul afficheur LVGL vivant
 * pendant toute l'exécution du harnais. */
static void initialiser_affichage_test(void)
{
    lv_init();
    lv_display_t *disp = lv_display_create(AFFICHAGE_TEST_LARGEUR, AFFICHAGE_TEST_HAUTEUR);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, g_tampon_affichage_test, NULL, sizeof(g_tampon_affichage_test),
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, vidage_test);
}

int main(void)
{
    initialiser_affichage_test();

    suite_harnais();
    suite_contrat();
    suite_moonraker_parse();
    suite_moonraker_rpc();
    suite_etat_store();
    suite_liaison();
    suite_backend_factice();
    suite_hote_parse();
    suite_boucle_cycle();
    suite_plateforme();
    suite_navigation();
    suite_habillage();
    suite_widgets();
    /* Tache 4 (jalon 3b) : le selecteur de pas, un widget partage de plus
     * (voir selecteur_pas.h) -- meme absence de contrainte d'ordre que
     * suite_widgets() ci-dessus, aucun etat process-wide partage. */
    suite_selecteur_pas();
    suite_ecran_accueil();
    suite_ecran_accueil_idle();
    suite_clavier();
    suite_ecran_configuration();
    /* Doit rester APRES suite_ecran_configuration() : sa derniere section
     * (echec asynchrone -> notification) reutilise l'habillage deja construit
     * par section_enregistrer() (singleton process-wide, voir le commentaire
     * de tete de test_commandes.c). Revue finale jalon 2b : cette dependance
     * d'ordre n'etait jusque-la garantie que par CE commentaire -- une
     * inversion accidentelle plantait en SEGV brut, sans le moindre compte de
     * verifications affiche. suite_commandes() verifie desormais elle-meme
     * habillage_est_construit() a son entree et arrete tout avec un message
     * clair si ce n'est pas le cas (voir habillage.h). */
    suite_commandes();
    /* Tache 11 : doit rester APRES suite_commandes() -- sa derniere section
     * reutilise la boucle simulee deja demarree par celle-ci (autre
     * singleton process-wide, voir le commentaire de tete de
     * test_jouet.c). Meme garde d'ordre que ci-dessus : suite_jouet()
     * verifie source_etat_sim_est_demarre() a son entree (voir
     * source_etat_sim.h). */
    suite_jouet();

    /* Tache 6 (jalon 3a) : l'ecran macros. DOIT rester APRES suite_commandes()
     * -- son dernier groupe (trace du seam) reutilise a la fois l'habillage
     * (construit par suite_ecran_configuration()) et la boucle simulee
     * (demarree par suite_commandes()), deux singletons process-wide. Meme
     * garde d'ordre que les autres : suite_ecran_macros() verifie elle-meme
     * les deux avant de s'appuyer dessus (voir son commentaire de tete). */
    suite_ecran_macros();

    /* Tache 4 (jalon 3b) : trace du seam pad de jog -> ui_commander(). Meme
     * garde d'ordonnancement que suite_ecran_macros() ci-dessus -- DOIT
     * rester APRES suite_commandes() (boucle simulee demarree) ET
     * suite_ecran_configuration() (habillage construit), voir le
     * commentaire de tete de suite_ecran_accueil_idle_jog(). */
    suite_ecran_accueil_idle_jog();

    /* Tache 5 (jalon 3b) : boutons de homing + confirmation. Meme garde
     * d'ordonnancement que suite_ecran_accueil_idle_jog() juste au-dessus
     * (boucle simulee demarree par suite_commandes()) -- voir le commentaire
     * de tete de suite_ecran_accueil_idle_home() dans test_ecran_accueil_idle.c. */
    suite_ecran_accueil_idle_home();

    /* Tache 6 (jalon 3b) : consignes de temperature manuelles + prereglages.
     * Meme garde d'ordonnancement que suite_ecran_accueil_idle_jog()/_home()
     * ci-dessus -- voir le commentaire de tete de
     * suite_ecran_accueil_idle_temp() dans test_ecran_accueil_idle.c. */
    suite_ecran_accueil_idle_temp();

    /* Pure, independante de toute autre suite (voir web_macros.h) : aucune
     * contrainte d'ordre. */
    suite_web_macros();

    /* Tache 4 (jalon 3a) : rejeu de fixtures enregistrees contre un vrai
     * Moonraker. Independante de toute autre suite (fichiers relus depuis
     * le disque, aucun etat process-wide partage) : aucune contrainte
     * d'ordre non plus. */
    suite_fixtures_moonraker();

    /* Tache 5 (jalon 3a) : la boite aux lettres portable, un slot sans
     * verrou -- independante de toute autre suite (aucun etat process-wide
     * partage). */
    suite_moonraker_boite();

    /* Tache 1 (jalon 3b) : couche de commande gcode pure (jog/home/temp),
     * fonctions pures sans aucun etat process-wide -- aucune contrainte
     * d'ordre avec les autres suites. */
    suite_klipper_gcode();

    /* Tache 2 (jalon 3b) : helper de palier d'affichage des outils, fonction
     * pure sans aucun etat process-wide -- aucune contrainte d'ordre avec les
     * autres suites. */
    suite_klipper_paliers();

    /* Tache 3 (jalon 3b) : helper pur de choix d'accueil au demarrage,
     * fonction pure sans aucun etat process-wide -- aucune contrainte
     * d'ordre avec les autres suites. */
    suite_accueil_choix();

    printf("\n%d verification(s), %d echec(s)\n", tests_lances, tests_echoues);
    return tests_echoues == 0 ? 0 : 1;
}
