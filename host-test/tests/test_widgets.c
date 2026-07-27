/* Suite de la tâche 5 : les deux widgets partagés (tuile de valeur, barre de
 * progression) et leurs formateurs purs (voir tuile.h / progression.h pour
 * le contrat).
 *
 * Deux volets : les formateurs (testables sans LVGL, sans regarder un
 * pixel — la vraie substance de cette tâche, voir task-5-brief.md) puis un
 * sourire minimal de création/mise à jour/grisage pour chaque widget,
 * exactement comme suite_navigation() le fait pour la pile d'écrans : la
 * cible `tests` a un afficheur hors écran initialisé dans main.c, donc
 * lv_obj_create() et consorts fonctionnent ici sans jamais dessiner un
 * pixel réel. */
#include <math.h>
#include <string.h>

#include "lvgl.h"

#include "petit_test.h"
#include "progression.h"
#include "tuile.h"

static void suite_formateurs(void)
{
    char b[16];
    printf("suite : widgets (formateurs)\n");

    ui_format_temperature(b, sizeof(b), 205.0f);   /* buse nominale */ VERIFIER_TEXTE(b, "205.0");
    ui_format_temperature(b, sizeof(b), 0.0f);     /* zero est une vraie mesure */ VERIFIER_TEXTE(b, "0.0");
    ui_format_temperature(b, sizeof(b), 59.94f);   /* arrondi au dixieme */ VERIFIER_TEXTE(b, "59.9");
    ui_format_temperature(b, sizeof(b), -12.0f);   /* negatif invraisemblable */ VERIFIER_TEXTE(b, "--");
    ui_format_temperature(b, sizeof(b), 999.0f);   /* au-dela du plausible */ VERIFIER_TEXTE(b, "--");
    ui_format_temperature(b, sizeof(b), NAN);      /* NaN */ VERIFIER_TEXTE(b, "--");
    ui_format_temperature(b, sizeof(b), INFINITY); /* infini */ VERIFIER_TEXTE(b, "--");

    ui_format_duree(b, sizeof(b), 0);       /* inconnu */ VERIFIER_TEXTE(b, "--");
    ui_format_duree(b, sizeof(b), 59);      /* moins d'une minute */ VERIFIER_TEXTE(b, "0m");
    ui_format_duree(b, sizeof(b), 83);      /* une minute */ VERIFIER_TEXTE(b, "1m");
    ui_format_duree(b, sizeof(b), 3599);    /* juste sous une heure */ VERIFIER_TEXTE(b, "59m");
    ui_format_duree(b, sizeof(b), 3600);    /* une heure pile */ VERIFIER_TEXTE(b, "1h 00m");
    ui_format_duree(b, sizeof(b), 3660);    /* juste au-dela d'une heure */ VERIFIER_TEXTE(b, "1h 01m");
    ui_format_duree(b, sizeof(b), 5025);    /* heures et minutes */ VERIFIER_TEXTE(b, "1h 23m");
    /* Borne haute de etat_klipper.h : KLIPPER_TEMPS_RESTANT_MAX_S = 359999. */
    ui_format_duree(b, sizeof(b), 359999u); /* borne haute */ VERIFIER_TEXTE(b, "99h 59m");

    /* Un tampon trop court ne doit jamais déborder ni laisser la chaîne
     * sans terminateur : le résultat est tronqué, la chaîne reste valide. */
    char court[4];
    ui_format_duree(court, sizeof(court), 5025);
    /* tampon court reste termine */ VERIFIER(court[3] == '\0');
}

static void suite_tuile(void)
{
    printf("suite : widgets (tuile)\n");

    /* Contexte alloué par l'écran, jamais par malloc (contrat du brief) :
     * ici une simple variable automatique, exactement comme le ferait le
     * contexte d'un écran calloc par navigation.c. */
    tuile_t t;
    memset(&t, 0, sizeof(t));

    tuile_creer(&t, lv_screen_active(), "Nozzle");
    /* les quatre champs publics sont remplis */
    VERIFIER(t.racine != NULL);
    VERIFIER(t.libelle != NULL);
    VERIFIER(t.valeur != NULL);
    VERIFIER(t.consigne != NULL);
    /* le libelle est pose des la creation */
    VERIFIER_TEXTE(lv_label_get_text(t.libelle), "Nozzle");

    tuile_definir_valeur(&t, "205.0");
    /* la valeur affichee est celle transmise, deja formatee par l'appelant */
    VERIFIER_TEXTE(lv_label_get_text(t.valeur), "205.0");

    tuile_definir_consigne(&t, "210.0");
    VERIFIER_TEXTE(lv_label_get_text(t.consigne), "210.0");

    /* Grisage : la couleur du texte change et reste réversible (pas un gris
     * a sens unique — lecon retenue de la revue de la tache 4, voir
     * habillage.c). */
    /* Comparaison via lv_color_eq(), pas lv_color_to_u32() == 0x6B7280 : sur
     * LV_COLOR_DEPTH 16 (voir simulateur/lv_conf.h), lv_color_hex() quantifie
     * en RGB565 dès la création et lv_color_to_u32() ne fait que reconstruire
     * une approximation par réplication de bits (même piège que la mire de
     * la tâche 1, task-1-report.md) — comparer deux couleurs déjà quantifiées
     * de la même façon est le test correct. */
    tuile_griser(&t, true);
    lv_color_t couleur_grise = lv_obj_get_style_text_color(t.valeur, 0);
    VERIFIER(lv_color_eq(couleur_grise, lv_color_hex(0x6B7280)));

    tuile_griser(&t, false);
    lv_color_t couleur_normale = lv_obj_get_style_text_color(t.valeur, 0);
    /* redevient different du gris une fois degrise */
    VERIFIER(!lv_color_eq(couleur_normale, lv_color_hex(0x6B7280)));

    /* NULL sur les setters ne plante pas (meme garde que tout le reste de
     * ui/, voir habillage_notifier()). */
    VERIFIER((tuile_definir_valeur(&t, NULL), true));
    VERIFIER_TEXTE(lv_label_get_text(t.valeur), "");
    VERIFIER((tuile_definir_consigne(&t, NULL), true));
    VERIFIER_TEXTE(lv_label_get_text(t.consigne), "");

    lv_obj_delete(t.racine);
}

static void suite_progression(void)
{
    printf("suite : widgets (progression)\n");

    progression_t p;
    memset(&p, 0, sizeof(p));

    progression_creer(&p, lv_screen_active());
    VERIFIER(p.racine != NULL);
    VERIFIER(p.barre != NULL);
    VERIFIER(p.etiquette != NULL);

    /* Regression (revue tache 5) : `racine` en LV_SIZE_CONTENT des deux cotes
     * pendant que `barre` (son enfant direct) est LV_PCT(100) est une
     * dependance circulaire que LVGL 9.2 resout en clouant l'enfant a zero
     * (voir simulateur/lvgl/src/core/lv_obj_pos.c:111-116 : "a pct-sized
     * child inside a content-sized parent [...] keep child size at zero").
     * Ca ne s'est jamais vu a l'oeil parce que simulateur/main.c redimensionne
     * toujours `racine` juste apres progression_creer(), avant la premiere
     * passe de mise en page — mais rien dans progression.h ne rend cet appel
     * obligatoire, et un ecran qui se contente de lv_obj_align() (exactement
     * ce que fait tuile_t, ou LV_SIZE_CONTENT fonctionne reellement puisque
     * ses enfants sont des labels de taille naturelle) obtiendrait une barre
     * en permanence invisible. lv_obj_update_layout() force la resolution
     * AVANT l'assertion : sans cet appel explicite, p.barre n'a encore subi
     * aucune passe de mise en page et le test ne prouverait rien. */
    lv_obj_update_layout(p.racine);
    /* la barre a une largeur reelle des la creation, sans redimensionnement
     * de racine par l'appelant */
    VERIFIER(lv_obj_get_width(p.barre) > 0);

    progression_definir(&p, 0.423f);
    /* la barre suit la fraction (plage interne 0..1000) */
    VERIFIER(lv_bar_get_value(p.barre) == 423);
    /* le pourcentage affiche a un decimale, au centre (mise en page tache 6) */
    VERIFIER_TEXTE(lv_label_get_text(p.etiquette), "42.3%");

    progression_definir(&p, 1.0f);
    VERIFIER_TEXTE(lv_label_get_text(p.etiquette), "100.0%");

    /* Une fraction hors [0, 1] ou non finie (meme categorie de defaut que la
     * conversion float -> uint32_t infinie deja rencontree dans ce jalon,
     * voir host-test/CMakeLists.txt) est bornee plutot que de faire tomber
     * la conversion vers l'entier interne du lv_bar sous UBSan. */
    progression_definir(&p, -5.0f);
    VERIFIER(lv_bar_get_value(p.barre) == 0);
    progression_definir(&p, 99.0f);
    VERIFIER(lv_bar_get_value(p.barre) == 1000);
    progression_definir(&p, NAN);
    VERIFIER(lv_bar_get_value(p.barre) == 0);
    progression_definir(&p, INFINITY);
    VERIFIER(lv_bar_get_value(p.barre) == 1000);
    progression_definir(&p, -INFINITY);
    VERIFIER(lv_bar_get_value(p.barre) == 0);

    progression_griser(&p, true);
    lv_color_t couleur_grise = lv_obj_get_style_text_color(p.etiquette, 0);
    VERIFIER(lv_color_eq(couleur_grise, lv_color_hex(0x6B7280)));
    progression_griser(&p, false);
    lv_color_t couleur_normale = lv_obj_get_style_text_color(p.etiquette, 0);
    VERIFIER(!lv_color_eq(couleur_normale, lv_color_hex(0x6B7280)));

    lv_obj_delete(p.racine);
}

void suite_widgets(void)
{
    suite_formateurs();
    suite_tuile();
    suite_progression();
}
