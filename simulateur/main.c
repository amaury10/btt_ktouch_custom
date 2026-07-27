/* Point d'entrée du simulateur : construit l'habillage (tâche 4 — barre
 * d'état, bandeau de notifications) et un écran jouet qui affiche l'état
 * factice, fait tourner la boucle simulée (source_etat_sim.c) contre
 * backend_factice (ou un backend qui échoue toujours, pour démontrer l'état
 * dégradé/hors ligne), puis soit capture le tout en PNG, soit l'affiche
 * dans une fenêtre SDL interactive.
 *
 * Remplace la mire de vérification de la tâche 1 (fond, quatre polices,
 * cadrage) plutôt que de la garder à côté sous un second mode : elle a
 * rempli son rôle une fois (voir task-1-report.md), et la garder vivante
 * ici forcerait soit un indicateur de ligne de commande supplémentaire pour
 * choisir entre les deux, soit deux chemins de code jamais tous deux
 * exercés le même jour. L'écran jouet ci-dessous, plus modeste qu'un futur
 * écran d'accueil Klipper (tâche 6), suffit à prouver que l'habillage
 * transmet réellement etat/generation/liaison à un écran empilé. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "afficheur.h"
#include "lvgl.h"

#include "backend.h"
#include "backend_factice.h"
#include "ecran.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "navigation.h"
#include "progression.h"
#include "source_etat_sim.h"
#include "tuile.h"

/* --- Écran de démonstration --------------------------------------------- *
 * Tâche 5 : le label de buse et la barre de progression "faits main" sont
 * remplacés par les widgets partagés (tuile_t / progression_t) — la
 * démonstration la plus simple qu'ils s'intègrent réellement à l'habillage
 * et au rendu SDL réel, pas seulement à l'afficheur hors écran 32x32 du
 * harnais de test (voir host-test/tests/test_widgets.c pour la preuve
 * mécanique, ceci pour la preuve visuelle). */

typedef struct {
    lv_obj_t     *etat_label;
    tuile_t       buse;
    progression_t progression;
} demo_ctx_t;

static void demo_construire(lv_obj_t *parent, void *contexte)
{
    demo_ctx_t *ctx = contexte;

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x10161D), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    ctx->etat_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->etat_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ctx->etat_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(ctx->etat_label, "...");
    lv_obj_align(ctx->etat_label, LV_ALIGN_TOP_MID, 0, 30);

    tuile_creer(&ctx->buse, parent, "Nozzle");
    lv_obj_set_size(ctx->buse.racine, 220, 140);
    lv_obj_align(ctx->buse.racine, LV_ALIGN_CENTER, 0, -30);

    progression_creer(&ctx->progression, parent);
    lv_obj_set_size(ctx->progression.racine, 500, 40);
    lv_obj_align(ctx->progression.racine, LV_ALIGN_CENTER, 0, 130);
}

/* Grise le label d'état et les deux widgets sur `donnees_perimees` (calculé
 * par l'habillage depuis la seule liaison, voir habillage_pomper() dans
 * firmware/main/ui/habillage.c) — c'est tout ce qu'un écran doit faire de ce
 * champ : ni boîte d'erreur, ni texte différent, juste une couleur plus
 * terne pour signaler que ces valeurs ne sont plus fraîches (spécification
 * §5.3, voir le commentaire de ecran.h sur `mettre_a_jour`). */
#define COULEUR_TEXTE_NORMAL_ETAT 0xFFFFFF
#define COULEUR_TEXTE_PERIME      0x6B7280

static void demo_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    demo_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    /* KLIPPER_FICHIER_MAX (64) + le reste du gabarit : marge large plutôt
     * qu'un calcul exact, pour ne pas avoir à revenir ici si le gabarit
     * change. */
    char tampon[64 + KLIPPER_FICHIER_MAX];
    char val[16];
    char consigne[16];

    snprintf(tampon, sizeof(tampon), "state: %s   file: %s", e->etat,
             e->fichier[0] != '\0' ? e->fichier : "-");
    lv_label_set_text(ctx->etat_label, tampon);

    ui_format_temperature(val, sizeof(val), e->buse_actuelle);
    ui_format_temperature(consigne, sizeof(consigne), e->buse_consigne);
    tuile_definir_valeur(&ctx->buse, val);
    tuile_definir_consigne(&ctx->buse, consigne);

    progression_definir(&ctx->progression, e->progression);

    lv_obj_set_style_text_color(
        ctx->etat_label,
        lv_color_hex(donnees_perimees ? COULEUR_TEXTE_PERIME : COULEUR_TEXTE_NORMAL_ETAT), 0);
    tuile_griser(&ctx->buse, donnees_perimees);
    progression_griser(&ctx->progression, donnees_perimees);
}

static const ecran_desc_t ECRAN_DEMO = {
    .id = "demo",
    .titre = "Demo",
    .taille_contexte = sizeof(demo_ctx_t),
    .construire = demo_construire,
    .mettre_a_jour = demo_mettre_a_jour,
    .detruire = NULL,
};

/* --- Backend qui échoue systématiquement -------------------------------- *
 * Sert uniquement à démontrer, dans le simulateur, l'état dégradé/hors
 * ligne de l'habillage : backend_factice.c ne produit jamais d'échec (voir
 * son en-tête), donc rien ne fait naturellement progresser liaison_t au-delà
 * de LIAISON_EN_LIGNE. Ce backend-jouet, local à ce fichier, ne touche
 * jamais le réseau — il rend ESP_FAIL immédiatement — et laisse
 * boucle_cycle()/liaison.c (le vrai code, pas une simulation de leur effet)
 * faire progresser la liaison vers DEGRADEE (3 échecs) puis HORS_LIGNE (10
 * échecs) exactement comme le ferait un hôte injoignable sur cible. */
static esp_err_t echec_demarrer(void *etat, const backend_hote_t *hote)
{
    (void)hote;
    memset(etat, 0, sizeof(etat_klipper_t));
    return ESP_OK;
}

static esp_err_t echec_rafraichir(void *etat)
{
    (void)etat;
    return ESP_FAIL;
}

static void echec_arreter(void *etat)
{
    (void)etat;
}

static esp_err_t echec_commande(void *etat, const char *action, const char *arguments_json)
{
    (void)etat;
    (void)action;
    (void)arguments_json;
    return ESP_ERR_NOT_SUPPORTED;
}

static const backend_desc_t BACKEND_ECHEC_DESC = {
    .nom = "echec-demo",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = echec_demarrer,
    .rafraichir = echec_rafraichir,
    .arreter = echec_arreter,
    .commande = echec_commande,
};

int main(int argc, char **argv)
{
    const char *chemin_capture = NULL;
    int cycles = 5;
    int scenario = 1;
    bool echec = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            chemin_capture = argv[++i];
        } else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--echec") == 0) {
            echec = true;
        }
    }

    afficheur_mode_t mode = (chemin_capture != NULL) ? AFFICHEUR_HORS_ECRAN : AFFICHEUR_FENETRE;
    if (!afficheur_demarrer(mode)) {
        fprintf(stderr, "echec du demarrage de l'afficheur (mode %s)\n",
                mode == AFFICHEUR_FENETRE ? "fenetre" : "hors ecran");
        return 1;
    }

    lv_obj_t *racine = lv_screen_active();
    habillage_construire(racine);
    navigation_empiler(&ECRAN_DEMO);

    const backend_desc_t *backend = echec ? &BACKEND_ECHEC_DESC : backend_factice_desc();
    if (!echec) {
        backend_factice_scenario(scenario);
    }
    if (!source_etat_sim_demarrer(backend)) {
        fprintf(stderr, "echec du demarrage de la boucle simulee\n");
    }

    if (chemin_capture != NULL) {
        /* --cycles fait avancer la boucle simulée d'autant de "secondes"
         * avant la capture : un cycle = un rafraîchissement du backend +
         * validation du magasin d'état, exactement ce que ferait
         * boucle_tache() une fois par seconde sur cible (voir
         * source_etat_sim.c). */
        for (int i = 0; i < cycles; i++) {
            source_etat_sim_cycle();
            habillage_pomper();
        }
        if (cycles == 0) {
            habillage_pomper();
        }
        if (cycles > 0) {
            habillage_notifier(echec ? "connection lost" : "host connected", echec);
        }

        /* Un cycle de pompe LVGL suffit à laisser rendre l'écran une
         * première fois avant la capture (délai nul : aucune animation
         * n'est en jeu ici, voir l'ancienne mire de la tâche 1). */
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
     * fenêtre par le gestionnaire de fenêtres). Un cycle de boucle simulée
     * par seconde écoulée, habillage_pomper() à chaque image — même ordre
     * que le brief impose pour la démonstration du simulateur. */
    uint32_t accumulateur_ms = 0;
    for (;;) {
        afficheur_pomper(16);
        habillage_pomper();
        usleep(16 * 1000);
        accumulateur_ms += 16;
        if (accumulateur_ms >= 1000) {
            source_etat_sim_cycle();
            accumulateur_ms = 0;
        }
    }
}
