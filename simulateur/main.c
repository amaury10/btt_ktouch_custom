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
#include "source_etat_sim.h"

/* --- Écran de démonstration --------------------------------------------- */

typedef struct {
    lv_obj_t *etat_label;
    lv_obj_t *buse_label;
    lv_obj_t *barre_progression;
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
    lv_obj_align(ctx->etat_label, LV_ALIGN_TOP_MID, 0, 40);

    ctx->buse_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->buse_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->buse_label, lv_color_hex(0xC9D1D9), 0);
    lv_label_set_text(ctx->buse_label, "");
    lv_obj_align(ctx->buse_label, LV_ALIGN_TOP_MID, 0, 100);

    ctx->barre_progression = lv_bar_create(parent);
    lv_obj_set_size(ctx->barre_progression, 500, 24);
    lv_obj_align(ctx->barre_progression, LV_ALIGN_CENTER, 0, 60);
    lv_bar_set_range(ctx->barre_progression, 0, 1000);
    lv_bar_set_value(ctx->barre_progression, 0, LV_ANIM_OFF);
}

/* Le grisage des données périmées (habillage_donnees_perimees(), règle 5.3)
 * revient à un écran réel de la tâche 6 ; cet écran jouet se contente
 * d'afficher le contenu brut pour prouver que le triplet état/génération/
 * liaison lu par habillage_pomper() atteint bien navigation_mettre_a_jour(). */
static void demo_mettre_a_jour(const void *etat, void *contexte)
{
    demo_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    /* KLIPPER_FICHIER_MAX (64) + le reste du gabarit : marge large plutôt
     * qu'un calcul exact, pour ne pas avoir à revenir ici si le gabarit
     * change. */
    char tampon[64 + KLIPPER_FICHIER_MAX];

    snprintf(tampon, sizeof(tampon), "state: %s", e->etat);
    lv_label_set_text(ctx->etat_label, tampon);

    snprintf(tampon, sizeof(tampon), "nozzle %.0f / %.0f C   file: %s", (double)e->buse_actuelle,
             (double)e->buse_consigne, e->fichier[0] != '\0' ? e->fichier : "-");
    lv_label_set_text(ctx->buse_label, tampon);

    lv_bar_set_value(ctx->barre_progression, (int32_t)(e->progression * 1000.0f), LV_ANIM_OFF);
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
