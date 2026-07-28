/* Suite de la tâche 6 : l'écran d'accueil Klipper (voir ecran_accueil.h pour
 * le contrat, et task-6-brief.md pour les scénarios exigés). Ce qui se teste
 * sans regarder un pixel : mettre_a_jour() ne plante sur aucun état
 * pathologique (dont le nom de fichier sans octet nul, voir le bloc dédié
 * plus bas), et les libellés lus par lv_label_get_text() valent ce qu'on
 * attend.
 *
 * Construction directe (calloc du contexte à la taille du descripteur, puis
 * ECRAN_ACCUEIL.construire()) plutôt que navigation_empiler() : ce fichier
 * teste UNIQUEMENT le contrat de cet écran, pas celui de la pile de
 * navigation (déjà couverte par test_navigation.c) -- navigation_empiler()
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
     * securite memoire.
     *
     * Une variable `etat_klipper_t` sur la PILE ne suffit pas a prouver quoi
     * que ce soit ici (revue tache 6, fix round 1, IMPORTANT 1) : `fichier`
     * vit a l'octet 40 d'une structure de 116 octets, et `progression` juste
     * apres a l'octet 104 -- une lecture non bornee de `fichier` deborde
     * DANS `progression`, un octet du MEME objet, pas au-dela de lui. ASan
     * n'instrumente pas les depassements intra-objet (seulement les bornes
     * de l'ALLOCATION entiere), donc `strlen(e->fichier)` peut lire jusqu'a
     * 76 octets de trop sans qu'ASan ne dise rien, tant qu'un octet nul
     * trouve par hasard plus loin dans le meme objet arrete la lecture avant
     * la fin de l'allocation. C'est exactement ce qui se produisait ici
     * avec `etat.progression = 1.0f` (deja ecrit plus haut dans cette
     * fonction) : en petit-boutiste, le premier octet de 1.0f est 0x00 --
     * strlen() s'arretait pile a la frontiere du champ (longueur 64) sans
     * jamais deborder, et l'assertion passait avec l'implementation SURE ET
     * l'implementation DANGEREUSE (`lv_label_set_text(ctx->fichier,
     * e->fichier)` sans copie bornee) : le test ne distinguait pas les deux.
     *
     * Pour forcer une VRAIE preuve, cet etat est alloue sur le TAS (pour que
     * ses limites correspondent aux redzones qu'ASan surveille) et rempli
     * d'un octet non nul partout AVANT d'ecrire `fichier` : aucun octet nul
     * ne subsiste nulle part dans les 116 octets de l'objet, donc une
     * lecture non bornee de `fichier` est GARANTIE de lire au moins 76
     * octets (64 + les 12 restants jusqu'a la fin de l'objet) avant de
     * pouvoir s'arreter -- largement au-dela de KLIPPER_FICHIER_MAX (64), ce
     * que l'assertion plus bas verifie. Aucun autre champ n'est retouche
     * apres ce point, deliberement : toute valeur "ronde" (0.5f, 1800u, ...)
     * a de bonnes chances de reintroduire un octet nul quelque part apres
     * `fichier` en representation IEEE754/entiere -- exactement la maniere
     * dont ce test s'est d'abord revele muet (voir plus haut :
     * `etat.progression = 1.0f` a 0x00 comme premier octet en
     * petit-boutiste). 0x7F7F7F7F reste un flottant fini enorme (ni NaN ni
     * infini : exposant 254, pas 255) et un uint32_t enorme mais valide --
     * ui_format_temperature()/ui_format_duree()/progression_definir()
     * tolerent deja n'importe quelle valeur finie sans planter (voir leurs
     * propres tests dans test_widgets.c), rien n'a donc besoin d'etre
     * "raisonnable" pour que cet appel soit sur. `impression_en_cours` reste
     * a 0x7F : mettre_a_jour() ne le lit jamais. `impression_en_pause`, en
     * revanche, DOIT etre remis a une valeur valide explicitement (tache 9,
     * revue) -- ecran_accueil.c le lit desormais pour faire basculer le
     * libelle Pause/Resume, et un octet 0x7F n'est pas une valeur valide de
     * `_Bool` : laisse tel quel, ce chargement s'arrete sous
     * -fsanitize=undefined ("load of value 127, which is not a valid value
     * for type '_Bool'") -- RED genuinement observe en ecrivant ce fichier
     * (revue de la tache 9, jalon 2b). Rouge du harnais de test, pas du code
     * de production : mettre_a_jour() lit un champ que le brief lui demande
     * desormais de lire, l'entree pathologique de CE test doit simplement
     * rester dans le domaine valide de _Bool sur ce champ precis.
     *
     * IMPORTANT (verifie a la revue de la tache 6, jalon 2b, "Fix round 1") :
     * passer par mettre_a_jour() -> lv_label_set_text() ici n'aboutit PAS a
     * un arret ASan, meme avec l'implementation dangereuse -- LVGL calcule
     * la longueur via lv_strlen(), une boucle maison
     * (simulateur/lvgl/src/stdlib/builtin/lv_string_builtin.c, choisie par
     * LV_USE_STDLIB_STRING=LV_STDLIB_BUILTIN dans simulateur/lv_conf.h), pas
     * la strlen() de la libc interceptee par ASan, et la cible CMake `lvgl`
     * est deliberement compilee SANS -fsanitize=address (voir
     * host-test/CMakeLists.txt). Un strlen() direct sur ce meme `e->fichier`
     * juste apres le remplissage ci-dessus PRODUIT bien
     * "AddressSanitizer: heap-buffer-overflow ... READ of size 77 ... 0
     * bytes after 116-byte region" (verifie manuellement, voir le rapport) :
     * la technique est saine, seule la route via LVGL echappe a ASan sur ce
     * projet precis -- raison pour laquelle l'assertion de longueur
     * ci-dessous, pas un arret ASan, est ce qui distingue reellement les
     * deux implementations ici. */
    etat_klipper_t *e = malloc(sizeof(*e));
    VERIFIER(e != NULL);
    memset(e, 0x7F, sizeof(*e));
    memset(e->fichier, 'x', KLIPPER_FICHIER_MAX);
    e->impression_en_pause = false; /* voir le commentaire ci-dessus */

    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(e, false, ctx), true));
    const char *nom_affiche = lv_label_get_text(ctx->fichier);
    VERIFIER(strlen(nom_affiche) <= KLIPPER_FICHIER_MAX);
    free(e);

    /* --- temps restant a la borne haute : "99h 59m", pas de debordement de
     * la conversion (meme classe de defaut que la borne deja rencontree
     * dans ce jalon, voir host-test/CMakeLists.txt sur float-cast-overflow). */
    etat.temps_restant_s = KLIPPER_TEMPS_RESTANT_MAX_S;
    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->temps), "99h 59m");

    /* --- perime : grise, puis redevient normal -- grisage reversible, meme
     * lecon que tuile_griser()/progression_griser() (voir test_widgets.c).
     * Bed (ctx->plateau) inclus explicitement (revue tache 6, fix round 1,
     * M-bed) : rien avant ce fix ne lisait jamais ctx->plateau, donc un
     * ecran.c qui aurait oublie tuile_griser(&ctx->plateau, ...) serait
     * reste vert. */
    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, true, ctx), true));
    lv_color_t gris_valeur = lv_obj_get_style_text_color(ctx->buse.valeur, 0);
    VERIFIER(lv_color_eq(gris_valeur, lv_color_hex(0x6B7280)));
    lv_color_t gris_plateau = lv_obj_get_style_text_color(ctx->plateau.valeur, 0);
    VERIFIER(lv_color_eq(gris_plateau, lv_color_hex(0x6B7280)));
    lv_color_t gris_fichier = lv_obj_get_style_text_color(ctx->fichier, 0);
    VERIFIER(lv_color_eq(gris_fichier, lv_color_hex(0x6B7280)));
    lv_color_t gris_temps = lv_obj_get_style_text_color(ctx->temps, 0);
    VERIFIER(lv_color_eq(gris_temps, lv_color_hex(0x6B7280)));
    lv_color_t gris_barre = lv_obj_get_style_text_color(ctx->progression.etiquette, 0);
    VERIFIER(lv_color_eq(gris_barre, lv_color_hex(0x6B7280)));

    VERIFIER((ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx), true));
    lv_color_t normal_valeur = lv_obj_get_style_text_color(ctx->buse.valeur, 0);
    VERIFIER(!lv_color_eq(normal_valeur, lv_color_hex(0x6B7280)));
    lv_color_t normal_plateau = lv_obj_get_style_text_color(ctx->plateau.valeur, 0);
    VERIFIER(!lv_color_eq(normal_plateau, lv_color_hex(0x6B7280)));
    lv_color_t normal_fichier = lv_obj_get_style_text_color(ctx->fichier, 0);
    VERIFIER(!lv_color_eq(normal_fichier, lv_color_hex(0x6B7280)));
    lv_color_t normal_temps = lv_obj_get_style_text_color(ctx->temps, 0);
    VERIFIER(!lv_color_eq(normal_temps, lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);
}
