/* Implementation : voir ecran_stub.h pour le contrat et pourquoi ces trois
 * panneaux se limitent a un titre + une ligne d'explication, sans aucune
 * donnee ni action -- et pourquoi les stubs d'origine Updater, Power et
 * Console ont quitte ce fichier pour ecran_updater.c (Task 2, jalon OTA
 * firmware), ecran_power.c (feature "Power devices Moonraker", tache B) et
 * ecran_console.c (feature "Console gcode", tache B) respectivement.
 *
 * STUBS(X) liste les trois panneaux restants (symbole, id, titre,
 * explication) ; X() est instanciee deux fois plus bas -- une premiere pour
 * generer les `<symbole>_construire()` (chacun ferme sur ses propres
 * litteraux, brief option (a)), une seconde pour generer les `ecran_desc_t`
 * eux-memes. Ajouter un stub de plus se resume a une ligne de plus dans
 * STUBS(). */
#include "ecran_stub.h"

#include "lvgl.h"

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9

/* Peint le fond + les deux labels centres, partage par les quatre construire()
 * generes plus bas -- seule la paire (titre, explication) change d'un stub a
 * l'autre. Positionnes vers le HAUT du contenu (742x436, sous la barre
 * d'etat construite par habillage.c -- meme repere que
 * ecran_niveau_lit.c/ecran_zcalibrate.c) : le second label termine bien
 * avant la bande couverte par le bandeau de notification (y absolu 420..480,
 * soit y relatif au contenu 376..436), aucun _Static_assert necessaire ici
 * faute de controle interactif a proteger (brief de la tache). */
static void ecran_stub_peindre(lv_obj_t *parent, const char *titre, const char *explication)
{
    if (parent == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_titre = lv_label_create(parent);
    lv_obj_set_style_text_font(label_titre, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label_titre, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_obj_set_style_text_align(label_titre, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label_titre, titre);
    lv_obj_align(label_titre, LV_ALIGN_TOP_MID, 0, 160);

    lv_obj_t *label_explication = lv_label_create(parent);
    lv_obj_set_style_text_font(label_explication, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_explication, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(label_explication, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label_explication, explication);
    lv_obj_align_to(label_explication, label_titre, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
}

/* Table maitresse des trois stubs restants -- symbole du descripteur, id
 * stable, titre KlipperScreen, ligne d'explication (valeurs EXACTES du brief
 * de la tache, verbatim). ECRAN_UPDATER n'y figure plus depuis Task 2 (jalon
 * OTA firmware) -- voir ecran_updater.c pour son remplacant reel. ECRAN_POWER
 * n'y figure plus depuis la feature "Power devices Moonraker" (tache B) --
 * voir ecran_power.c pour son remplacant reel. ECRAN_CONSOLE n'y figure plus
 * depuis la feature "Console gcode" (tache B) -- voir ecran_console.c pour
 * son remplacant reel. */
#define STUBS(X)                                                                                                     \
    X(ECRAN_SPOOLMAN, "spoolman", "Spoolman", "Requires a Spoolman server - not yet available")

/* Un `<symbole>_construire()` par stub -- signature imposee par
 * ecran_desc_t.construire (ecran.h), `contexte` ignore (taille_contexte = 0,
 * voir plus bas : "un ecran purement statique... peut laisser ce pointeur a
 * NULL"). */
#define DEFINIR_CONSTRUIRE(symbole, id, titre, explication)                                                          \
    static void symbole##_construire(lv_obj_t *parent, void *contexte)                                              \
    {                                                                                                                \
        (void)contexte;                                                                                              \
        ecran_stub_peindre(parent, titre, explication);                                                            \
    }
STUBS(DEFINIR_CONSTRUIRE)
#undef DEFINIR_CONSTRUIRE

/* Les quatre descripteurs -- mettre_a_jour/detruire a NULL (rien de dynamique,
 * rien a liberer), meme choix que ecran_niveau_lit.c.
 *
 * ATTENTION : les parametres de macro NE PEUVENT PAS s'appeler `id`/`titre`
 * ici -- le preprocesseur substitue token par token, y compris apres un
 * `.`, donc `.id = id,` avec un parametre nomme `id` deviendrait
 * `."power" = "power",` (constat direct : premiere tentative, echec de
 * compilation "expected identifier before string constant"). Suffixe
 * `_valeur` pour lever toute collision avec les designateurs `.id`/`.titre`
 * de ecran_desc_t. */
#define DEFINIR_DESC(symbole, id_valeur, titre_valeur, explication_valeur)                                           \
    const ecran_desc_t symbole = {                                                                                   \
        .id = id_valeur,                                                                                             \
        .titre = titre_valeur,                                                                                       \
        .taille_contexte = 0,                                                                                        \
        .construire = symbole##_construire,                                                                         \
        .mettre_a_jour = NULL,                                                                                       \
        .detruire = NULL,                                                                                            \
    };
STUBS(DEFINIR_DESC)
#undef DEFINIR_DESC

#undef STUBS
