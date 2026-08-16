/* Implémentation : voir ecran_parc.h pour le contrat.
 *
 * Mise en page (742x436, conteneur de navigation) : grille 2 colonnes x 3
 * lignes de tuiles-boutons pleine hauteur de cellule -- une par imprimante
 * configurée, la suivante « + Add printer » si le parc n'est pas plein.
 * Libellé multi-ligne : nom en première ligne, état/températures en
 * seconde. L'ACTIVE porte une bordure verte (même vert que la pastille
 * EN_LIGNE de l'habillage) et lit l'état temps réel ; les autres lisent la
 * sonde (parc_imprimantes.h). Grille reconstruite SYSTÉMATIQUEMENT à chaque
 * changement de génération, jamais incrémentale (discipline ecran_usb.c). */
#include "ecran_parc.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_system.h" /* esp_restart -- la bascule d'imprimante redémarre la dalle */
#endif

#include "clavier.h"
#include "confirmation.h"
#include "ecran_configuration.h" /* ecran_configuration_valider() -- jamais un second analyseur */
#include "etat_klipper.h"
#include "habillage.h" /* habillage_notifier() -- refus/erreurs de saisie */
#include "journal.h"
#include "parc_sonde.h"
#include "source_reglages.h" /* ui_reglages_definir_hote() -- la bascule recopie l'hote actif */

static const char *TAG = "ecran_parc";

#define LARGEUR_CONTENU 742
#define HAUTEUR_CONTENU 436
#define MARGE 14

#define GRILLE_COLONNES 2
#define GRILLE_LIGNES   3
#define GRILLE_ECART    10

#define TUILE_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - (GRILLE_COLONNES - 1) * GRILLE_ECART) / GRILLE_COLONNES)
#define TUILE_HAUTEUR ((HAUTEUR_CONTENU - 2 * MARGE - (GRILLE_LIGNES - 1) * GRILLE_ECART) / GRILLE_LIGNES)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_ACTIVE           0x2ECC71 /* même vert que la pastille EN_LIGNE, habillage.c */

_Static_assert(PARC_MAX <= GRILLE_COLONNES * GRILLE_LIGNES,
               "plus d'imprimantes que la grille 2x3 n'en affiche");

/* Position d'une tuile dans la grille (indice 0..PARC_MAX-1, ordre ligne
 * par ligne). L'« + Add » occupe l'emplacement suivant la dernière entrée. */
static void tuile_position(uint8_t indice, lv_coord_t *x, lv_coord_t *y)
{
    *x = MARGE + (lv_coord_t)(indice % GRILLE_COLONNES) * (TUILE_LARGEUR + GRILLE_ECART);
    *y = MARGE + (lv_coord_t)(indice / GRILLE_COLONNES) * (TUILE_HAUTEUR + GRILLE_ECART);
}

/* --- bascule d'imprimante ------------------------------------------------ */

static void rappel_confirmer_bascule(bool confirme, void *contexte)
{
    ecran_parc_ctx_t *ctx = contexte;
    if (!confirme || ctx == NULL) {
        return;
    }
    uint8_t indice = ctx->indice_attente;
    if (indice >= ctx->config.nb) {
        return; /* la config a changé sous le dialogue : ne rien faire */
    }

    /* ORDRE de la bascule (revue du 2026-08-15, L6) : l'hôte de boot
       D'ABORD (c'est LA source de vérité, voir parc_imprimantes.h), le
       marqueur actif du parc ENSUITE -- et rollback best-effort de l'hôte
       si le parc refuse, pour ne jamais laisser les deux persister en
       désaccord. */
    backend_hote_t ancien_hote;
    bool ancien_valide = ui_reglages_hote(&ancien_hote);
    if (ui_reglages_definir_hote(&ctx->config.entrees[indice].hote) != ESP_OK) {
        habillage_notifier("Switch failed (settings write)", true);
        return;
    }
    parc_config_t config = ctx->config;
    config.actif = indice;
    if (parc_config_definir(&config) != ESP_OK) {
        if (ancien_valide) {
            (void)ui_reglages_definir_hote(&ancien_hote); /* rollback best-effort */
        }
        habillage_notifier("Switch failed (store unavailable)", true);
        return;
    }

    JOURNAL_INFO(TAG, "bascule vers %s (%s:%u), redemarrage",
                 config.entrees[indice].nom, config.entrees[indice].hote.adresse,
                 (unsigned)config.entrees[indice].hote.port);
#ifdef ESP_PLATFORM
    /* Le redémarrage EST le chemin d'application d'un changement d'hôte
       (voir ecran_configuration.c : « power-cycle pour l'instant »).
       lv_refr_now() et PAS un vTaskDelay (revue L5) : ce rappel tourne SOUS
       lv_timer_handler -- un délai ici bloquerait précisément le rendu
       qu'il prétend attendre, la notification ne serait jamais peinte. */
    habillage_notifier("Switching printer, restarting...", false);
    lv_refr_now(NULL);
    esp_restart();
#else
    habillage_notifier("Switch saved (restart applies it)", false);
#endif
}

static void tuile_cb(lv_event_t *e)
{
    ecran_parc_emplacement_t *info = lv_event_get_user_data(e);
    if (info == NULL || info->ctx == NULL) {
        return;
    }
    ecran_parc_ctx_t *ctx = info->ctx;
    uint8_t indice = info->indice;
    if (indice >= ctx->config.nb) {
        return;
    }
    /* LVGL envoie LV_EVENT_CLICKED au relachement MEME quand
       LV_EVENT_LONG_PRESSED vient de partir : sans cette garde, un appui long
       ouvrirait le menu d'actions puis, au relachement, tenterait d'ouvrir
       "Switch printer?" par-dessus (refuse avec une alerte au journal par le
       singleton de confirmation.c -- sur, mais bruyant et trompeur). */
    if (confirmation_est_ouverte()) {
        return;
    }
    if (indice == ctx->config.actif) {
        return; /* déjà active : rien à faire */
    }
    ctx->indice_attente = indice;
    confirmation_ouvrir("Switch printer?", ctx->config.entrees[indice].nom, "Switch",
                        /*destructif=*/false, rappel_confirmer_bascule, ctx);
}

/* --- appui long sur une tuile : editer l'adresse / retirer --------------- *
 * Le tap simple bascule (voir tuile_cb ci-dessus) ; l'appui long ouvre les
 * deux autres issues. C'est le SEUL chemin de suppression du firmware, et le
 * seul moyen de corriger l'adresse d'une imprimante depuis l'ecran. */

static void rappel_editer_adresse(const char *valeur, void *contexte)
{
    ecran_parc_ctx_t *ctx = contexte;
    if (ctx == NULL || valeur == NULL || valeur[0] == '\0') {
        return; /* annule/vide : abandon silencieux, comme partout */
    }
    backend_hote_t hote;
    char erreur[64];
    if (!ecran_configuration_valider(valeur, &hote, erreur, sizeof(erreur))) {
        habillage_notifier(erreur, true);
        return;
    }

    parc_config_t config;
    parc_config_lire(&config);
    uint8_t indice = ctx->indice_attente;
    if (indice >= config.nb) {
        return; /* la config a change sous le clavier : ne rien faire */
    }
    config.entrees[indice].hote = hote;
    if (parc_config_definir(&config) != ESP_OK) {
        habillage_notifier("Edit failed (store unavailable)", true);
        return;
    }

    /* Imprimante ACTIVE : l'hote de boot est LA source de verite (voir
       parc_imprimantes.h et l'ordre de rappel_confirmer_bascule ci-dessus).
       Sans cette seconde ecriture, la nouvelle adresse ne prendrait effet
       qu'apres un aller-retour de bascule -- l'edition paraitrait sans effet.
       Pas de redemarrage automatique : la dalle parle encore a l'ancienne
       adresse jusqu'au prochain demarrage, et le message le dit. */
    if (indice == config.actif) {
        if (ui_reglages_definir_hote(&hote) != ESP_OK) {
            habillage_notifier("Address saved for the list only", true);
            return;
        }
        habillage_notifier("Address saved (restart applies it)", false);
        return;
    }
    habillage_notifier("Address saved", false);
}

static void rappel_action_tuile(int choix, void *contexte)
{
    ecran_parc_ctx_t *ctx = contexte;
    if (ctx == NULL || choix < 0) {
        return; /* annulation */
    }
    uint8_t indice = ctx->indice_attente;
    parc_config_t config;
    parc_config_lire(&config);
    if (indice >= config.nb) {
        return; /* la config a change sous le dialogue */
    }

    if (choix == 0) {
        /* Editer l'adresse : clavier preverni de la valeur courante, meme
           format "adresse:port" que celui qu'attend ecran_configuration_valider(). */
        char courant[BACKEND_HOTE_LONGUEUR_MAX + 8];
        snprintf(courant, sizeof(courant), "%s:%u",
                 config.entrees[indice].hote.adresse,
                 (unsigned)config.entrees[indice].hote.port);
        clavier_ouvrir("Printer address", courant, CLAVIER_TEXTE, rappel_editer_adresse, ctx);
        return;
    }

    /* Retrait. parc_config_retirer() refuse l'active tant qu'il reste
       d'autres imprimantes (voir son contrat) : on traduit ce refus en
       consigne, plutot qu'en echec muet. */
    esp_err_t erreur = parc_config_retirer(&config, indice);
    if (erreur == ESP_ERR_INVALID_STATE) {
        habillage_notifier("Switch to another printer first", true);
        return;
    }
    if (erreur != ESP_OK) {
        habillage_notifier("Remove failed", true);
        return;
    }
    if (parc_config_definir(&config) != ESP_OK) {
        habillage_notifier("Remove failed (store unavailable)", true);
        return;
    }
    habillage_notifier("Printer removed", false);
}

static void tuile_long_cb(lv_event_t *e)
{
    ecran_parc_emplacement_t *info = lv_event_get_user_data(e);
    if (info == NULL || info->ctx == NULL) {
        return;
    }
    ecran_parc_ctx_t *ctx = info->ctx;
    uint8_t indice = info->indice;
    if (indice >= ctx->config.nb || confirmation_est_ouverte()) {
        return;
    }
    ctx->indice_attente = indice;

    char message[BACKEND_HOTE_LONGUEUR_MAX + 8];
    snprintf(message, sizeof(message), "%s:%u",
             ctx->config.entrees[indice].hote.adresse,
             (unsigned)ctx->config.entrees[indice].hote.port);
    confirmation_ouvrir_choix(ctx->config.entrees[indice].nom, message,
                              "Edit address", "Remove", /*destructif_b=*/true,
                              rappel_action_tuile, ctx);
}

/* --- ajout d'une imprimante (clavier nom puis adresse) ------------------- */

static void rappel_adresse(const char *valeur, void *contexte)
{
    ecran_parc_ctx_t *ctx = contexte;
    if (ctx == NULL || valeur == NULL || valeur[0] == '\0') {
        return; /* annulé/vide : abandon silencieux, comme partout */
    }
    backend_hote_t hote;
    char erreur[64];
    if (!ecran_configuration_valider(valeur, &hote, erreur, sizeof(erreur))) {
        habillage_notifier(erreur, true);
        return;
    }

    parc_config_t config;
    parc_config_lire(&config);
    if (config.nb >= PARC_MAX) {
        habillage_notifier("Printer list is full", true);
        return;
    }
    bool premiere = (config.nb == 0);
    parc_entree_t *entree = &config.entrees[config.nb];
    memset(entree, 0, sizeof(*entree));
    snprintf(entree->nom, sizeof(entree->nom), "%s", ctx->nom_attente);
    entree->hote = hote;
    config.nb++;
    if (parc_config_definir(&config) != ESP_OK) {
        habillage_notifier("Add failed (store unavailable)", true);
        return;
    }
    /* Première imprimante d'un parc vide (revue du 2026-08-15, L3) : elle
       devient active de fait (actif == 0) -- il FAUT donc aussi écrire
       l'hôte de boot, sinon la tuile s'affiche « active » sans que la dalle
       ne s'y connecte jamais (et taper une tuile active est un no-op :
       aucune issue). Pas de redémarrage automatique à l'ajout : notifié. */
    if (premiere) {
        if (ui_reglages_definir_hote(&hote) == ESP_OK) {
            habillage_notifier("Printer saved - restart to connect", false);
        } else {
            habillage_notifier("Saved, but boot host write failed", true);
        }
    }
    /* La génération vient de bouger : mettre_a_jour() reconstruira. */
}

static void rappel_nom(const char *valeur, void *contexte)
{
    ecran_parc_ctx_t *ctx = contexte;
    if (ctx == NULL || valeur == NULL || valeur[0] == '\0') {
        return;
    }
    snprintf(ctx->nom_attente, sizeof(ctx->nom_attente), "%s", valeur);
    /* Rouvrir un clavier depuis un rappel de clavier est un usage prévu du
       widget (voir clavier.h : le rappel court APRÈS la programmation de la
       destruction du précédent). Même mode que l'adresse de
       ecran_configuration.c (CLAVIER_NUMERIQUE : des IP surtout). */
    clavier_ouvrir("Printer address (host:port)", "192.168.", CLAVIER_NUMERIQUE, rappel_adresse, ctx);
}

static void bouton_ajouter_cb(lv_event_t *e)
{
    ecran_parc_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    clavier_ouvrir("Printer name", "", CLAVIER_TEXTE, rappel_nom, ctx);
}

/* --- rendu --------------------------------------------------------------- */

/* Compose le libellé d'une tuile : nom + ligne d'état. `actif` pioche dans
 * le résumé temps réel, les autres dans la sonde. */
static void tuile_libeller(ecran_parc_ctx_t *ctx, uint8_t indice)
{
    const parc_entree_t *entree = &ctx->config.entrees[indice];
    char ligne_etat[64];

    if (indice == ctx->config.actif) {
        if (ctx->actif_disponible) {
            snprintf(ligne_etat, sizeof(ligne_etat), "%s  %.0f\xC2\xB0 / %.0f\xC2\xB0",
                     ctx->etat_actif[0] != '\0' ? ctx->etat_actif : "connecting",
                     (double)ctx->buse_actif, (double)ctx->lit_actif);
        } else {
            snprintf(ligne_etat, sizeof(ligne_etat), "connecting...");
        }
    } else {
        const parc_etat_t *sonde = &ctx->etats[indice];
        if (!sonde->sonde) {
            snprintf(ligne_etat, sizeof(ligne_etat), "?");
        } else if (!sonde->atteignable) {
            snprintf(ligne_etat, sizeof(ligne_etat), "unreachable");
        } else if (sonde->progression_pct > 0 && strcmp(sonde->etat, "printing") == 0) {
            snprintf(ligne_etat, sizeof(ligne_etat), "%s %u%%  %.0f\xC2\xB0 / %.0f\xC2\xB0",
                     sonde->etat, (unsigned)sonde->progression_pct,
                     (double)sonde->buse, (double)sonde->lit);
        } else {
            snprintf(ligne_etat, sizeof(ligne_etat), "%s  %.0f\xC2\xB0 / %.0f\xC2\xB0",
                     sonde->etat[0] != '\0' ? sonde->etat : "?",
                     (double)sonde->buse, (double)sonde->lit);
        }
    }

    char libelle[PARC_NOM_MAX + sizeof(ligne_etat) + 8];
    snprintf(libelle, sizeof(libelle), "%s\n%s", entree->nom, ligne_etat);
    lv_label_set_text(ctx->labels[indice], libelle);
}

static void afficher(ecran_parc_ctx_t *ctx)
{
    bool aucun = (ctx->config.nb == 0);
    if (ctx->vide != NULL) {
        if (aucun) {
            lv_obj_clear_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (uint8_t i = 0; i < PARC_MAX; i++) {
        if (i < ctx->config.nb) {
            tuile_libeller(ctx, i);
            lv_obj_set_style_border_color(ctx->tuiles[i],
                                          lv_color_hex(i == ctx->config.actif ? COULEUR_ACTIVE
                                                                              : COULEUR_BOUTON),
                                          0);
            lv_obj_set_style_border_width(ctx->tuiles[i], i == ctx->config.actif ? 3 : 1, 0);
            lv_obj_clear_flag(ctx->tuiles[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->tuiles[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (ctx->bouton_ajouter != NULL) {
        if (ctx->config.nb < PARC_MAX) {
            lv_coord_t x;
            lv_coord_t y;
            tuile_position(ctx->config.nb, &x, &y);
            lv_obj_set_pos(ctx->bouton_ajouter, x, y);
            lv_obj_clear_flag(ctx->bouton_ajouter, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->bouton_ajouter, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_obj_t *tuile_creer(lv_obj_t *parent, lv_obj_t **label_sortie)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, TUILE_LARGEUR, TUILE_HAUTEUR);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(bouton, 1, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 10, 0);

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, TUILE_LARGEUR - 16);
    lv_label_set_text(label, "");
    lv_obj_center(label);

    if (label_sortie != NULL) {
        *label_sortie = label;
    }
    return bouton;
}

static void ecran_parc_construire(lv_obj_t *parent, void *contexte)
{
    ecran_parc_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    /* Sonde : démarrage paresseux (RAM saine à la première ouverture) puis
       armement -- désarmée à detruire(), aucun trafic écran fermé. */
    parc_sonde_demarrage_paresseux();
    parc_sonde_activer(true);

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    ctx->vide = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->vide, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->vide, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->vide, "No printers configured");
    lv_obj_center(ctx->vide);
    lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < PARC_MAX; i++) {
        lv_coord_t x;
        lv_coord_t y;
        tuile_position(i, &x, &y);
        ctx->tuiles[i] = tuile_creer(parent, &ctx->labels[i]);
        lv_obj_set_pos(ctx->tuiles[i], x, y);
        lv_obj_add_flag(ctx->tuiles[i], LV_OBJ_FLAG_HIDDEN);
        ctx->emplacements[i].ctx = ctx;
        ctx->emplacements[i].indice = i;
        lv_obj_add_event_cb(ctx->tuiles[i], tuile_cb, LV_EVENT_CLICKED, &ctx->emplacements[i]);
        /* Appui long : editer l'adresse / retirer (voir tuile_long_cb). Meme
           user_data que le clic, les deux rappels lisent le meme emplacement. */
        lv_obj_add_event_cb(ctx->tuiles[i], tuile_long_cb, LV_EVENT_LONG_PRESSED, &ctx->emplacements[i]);
    }

    lv_obj_t *label_ajouter = NULL;
    ctx->bouton_ajouter = tuile_creer(parent, &label_ajouter);
    lv_obj_set_style_text_color(label_ajouter, lv_color_hex(COULEUR_GRISE), 0);
    lv_label_set_text(label_ajouter, LV_SYMBOL_PLUS " Add printer");
    lv_obj_add_flag(ctx->bouton_ajouter, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ctx->bouton_ajouter, bouton_ajouter_cb, LV_EVENT_CLICKED, ctx);

    ctx->derniere_generation = 0;
    ctx->actif_disponible = false;
    ctx->etat_actif[0] = '\0';
    ctx->nom_attente[0] = '\0';

    parc_config_lire(&ctx->config);
    parc_etats_lire(ctx->etats);
    afficher(ctx);
    ctx->derniere_generation = parc_generation();
}

static void ecran_parc_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_parc_ctx_t *ctx = contexte;
    if (ctx == NULL) {
        return;
    }

    /* Résumé temps réel de l'ACTIVE -- toujours réévalué (l'état Klipper
       bouge sans toucher la génération du parc). */
    const etat_klipper_t *klipper = etat;
    bool disponible_avant = ctx->actif_disponible;
    char etat_avant[sizeof(ctx->etat_actif)];
    memcpy(etat_avant, ctx->etat_actif, sizeof(etat_avant));
    float buse_avant = ctx->buse_actif;
    float lit_avant = ctx->lit_actif; /* revue L8 : le lit seul doit aussi redessiner */

    ctx->actif_disponible = (klipper != NULL) && !donnees_perimees;
    if (klipper != NULL) {
        /* Copie bornee manuelle, PAS snprintf "%s" : klipper->etat est plus
           large que etat_actif, gcc (-Werror=format-truncation) refuse --
           meme piege, meme correctif que ecran_usb.c/ecran_fichiers.c. */
        size_t longueur = strlen(klipper->etat);
        if (longueur >= sizeof(ctx->etat_actif)) {
            longueur = sizeof(ctx->etat_actif) - 1;
        }
        memcpy(ctx->etat_actif, klipper->etat, longueur);
        ctx->etat_actif[longueur] = '\0';
        uint8_t outil = (klipper->outil_actif < klipper->nb_extrudeurs) ? klipper->outil_actif : 0;
        ctx->buse_actif = klipper->extrudeurs[outil].actuelle;
        ctx->lit_actif = klipper->plateau.actuelle;
    }

    uint32_t generation = parc_generation();
    bool parc_change = (generation != ctx->derniere_generation);
    if (parc_change) {
        parc_config_lire(&ctx->config);
        parc_etats_lire(ctx->etats);
        ctx->derniere_generation = generation;
    }

    /* Redessin si le parc a changé OU si le résumé de l'active a bougé --
       jamais à chaque pompage pour rien (comparaison grossière volontaire :
       état + disponibilité + buse au dixième près suffisent, la sonde
       rafraîchit de toute façon les autres tuiles par génération). */
    bool actif_change = (disponible_avant != ctx->actif_disponible) ||
                        (strcmp(etat_avant, ctx->etat_actif) != 0) ||
                        (klipper != NULL && (ctx->buse_actif - buse_avant > 0.5f ||
                                             buse_avant - ctx->buse_actif > 0.5f)) ||
                        (klipper != NULL && (ctx->lit_actif - lit_avant > 0.5f ||
                                             lit_avant - ctx->lit_actif > 0.5f));
    if (parc_change || actif_change) {
        afficher(ctx);
    }
}

static void ecran_parc_detruire(void *contexte)
{
    (void)contexte;
    /* Sonde désarmée : aucun trafic de fond une fois l'écran quitté (elle
       finit au plus son tour en cours, <= ~2,5 s). */
    parc_sonde_activer(false);
}

const ecran_desc_t ECRAN_PARC = {
    .id = "parc",
    .titre = "Printers",
    .taille_contexte = sizeof(ecran_parc_ctx_t),
    .construire = ecran_parc_construire,
    .mettre_a_jour = ecran_parc_mettre_a_jour,
    .detruire = ecran_parc_detruire,
};
