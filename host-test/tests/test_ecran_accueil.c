/* Suite de la tâche 6 : l'écran d'accueil Klipper (voir ecran_accueil.h pour
 * le contrat, et task-6-brief.md pour les scénarios exigés). Ce qui se teste
 * sans regarder un pixel : mettre_a_jour() ne plante sur aucun état
 * pathologique (dont le nom de fichier sans octet nul -- le cas de sécurité
 * mémoire qu'ASan surveille ici, voir host-test/CMakeLists.txt), et les
 * libellés lus par lv_label_get_text() valent ce qu'on attend.
 *
 * Construction directe (calloc du contexte à la taille du descripteur, puis
 * ECRAN_ACCUEIL.construire()) plutôt que navigation_empiler() : ce fichier
 * teste UNIQUEMENT le contrat de cet écran, pas celui de la pile de
 * navigation (déjà couvert par test_navigation.c) -- navigation_empiler()
 * ajouterait une dépendance à navigation_init()/lv_screen_active() sans
 * rien prouver de plus sur ecran_accueil.c lui-même. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ecran_accueil.h"
#include "etat_klipper.h"
#include "petit_test.h"

void suite_ecran_accueil(void)
{
    printf("suite : ecran accueil\n");

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_ctx_t *ctx = (ecran_accueil_ctx_t *)brut;

    ECRAN_ACCUEIL.construire(parent, ctx);
    /* tous les widgets sont crees */
    VERIFIER(ctx->buse.racine != NULL);
    VERIFIER(ctx->plateau.racine != NULL);
    VERIFIER(ctx->fichier != NULL);
    VERIFIER(ctx->progression.racine != NULL);
    VERIFIER(ctx->temps != NULL);
    VERIFIER(ctx->bouton_pause != NULL);
    VERIFIER(ctx->bouton_annuler != NULL);
    VERIFIER(ctx->bouton_urgence != NULL);

    /* --- etat entierement a zero : rien ne doit planter ; temps restant a 0
     * encode "inconnu" (voir KLIPPER_TEMPS_RESTANT_MAX_S dans
     * etat_klipper.h), 0.0 C est une mesure reelle (pas "--"), fichier vide. */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->buse.valeur), "0.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->plateau.valeur), "0.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->fichier), "");
    VERIFIER_TEXTE(lv_label_get_text(ctx->progression.etiquette), "0.0%");
    VERIFIER_TEXTE(lv_label_get_text(ctx->temps), "--");

    /* --- progression a 100% */
    etat.progression = 1.0f;
    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->progression.etiquette), "100.0%");

    /* --- temperatures aberrantes : "--", jamais un nombre faux (meme regle
     * que ui_format_temperature(), voir tuile.h). */
    etat.buse_actuelle = 999.0f;
    etat.plateau_actuel = -999.0f;
    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->buse.valeur), "--");
    VERIFIER_TEXTE(lv_label_get_text(ctx->plateau.valeur), "--");

    /* --- nom de fichier occupant tout le champ SANS octet nul : le cas de
     * securite memoire. Si l'ecran passait e->fichier directement a
     * lv_label_set_text() (qui appelle strlen()), ASan verrait une lecture
     * hors bornes ici et ferait tomber tout le harnais. */
    memset(etat.fichier, 'x', KLIPPER_FICHIER_MAX);
    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx), true));
    const char *nom_affiche = lv_label_get_text(ctx->fichier);
    VERIFIER(strlen(nom_affiche) <= KLIPPER_FICHIER_MAX);

    /* --- temps restant a la borne haute : "99h 59m", pas de debordement de
     * la conversion (meme classe de defaut que la borne deja rencontree
     * dans ce jalon, voir host-test/CMakeLists.txt sur float-cast-overflow). */
    etat.temps_restant_s = KLIPPER_TEMPS_RESTANT_MAX_S;
    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->temps), "99h 59m");

    /* --- perime : grise, puis redevient normal -- grisage reversible, meme
     * lecon que tuile_griser()/progression_griser() (voir test_widgets.c). */
    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, true, ctx), true));
    lv_color_t gris_valeur = lv_obj_get_style_text_color(ctx->buse.valeur, 0);
    VERIFIER(lv_color_eq(gris_valeur, lv_color_hex(0x6B7280)));
    lv_color_t gris_fichier = lv_obj_get_style_text_color(ctx->fichier, 0);
    VERIFIER(lv_color_eq(gris_fichier, lv_color_hex(0x6B7280)));
    lv_color_t gris_temps = lv_obj_get_style_text_color(ctx->temps, 0);
    VERIFIER(lv_color_eq(gris_temps, lv_color_hex(0x6B7280)));
    lv_color_t gris_barre = lv_obj_get_style_text_color(ctx->progression.etiquette, 0);
    VERIFIER(lv_color_eq(gris_barre, lv_color_hex(0x6B7280)));

    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx), true));
    lv_color_t normal_valeur = lv_obj_get_style_text_color(ctx->buse.valeur, 0);
    VERIFIER(!lv_color_eq(normal_valeur, lv_color_hex(0x6B7280)));
    lv_color_t normal_fichier = lv_obj_get_style_text_color(ctx->fichier, 0);
    VERIFIER(!lv_color_eq(normal_fichier, lv_color_hex(0x6B7280)));
    lv_color_t normal_temps = lv_obj_get_style_text_color(ctx->temps, 0);
    VERIFIER(!lv_color_eq(normal_temps, lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);
}
