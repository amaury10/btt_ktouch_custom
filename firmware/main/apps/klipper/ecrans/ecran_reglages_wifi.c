/* Implémentation : voir ecran_reglages_wifi.h pour le contrat.
 *
 * Mise en page (742x436, sous la barre d'état construite par habillage.c, à
 * droite du rail persistant -- d'où LARGEUR_CONTENU=742, comme ecran_accueil.c
 * et non les 800 de ecran_macros.c/ecran_configuration.c, antérieurs au rail) :
 * une ligne d'en-tête (état de connexion à gauche + bouton « Rescan » à droite),
 * une liste de réseaux (une ligne pleine largeur par réseau : SSID + cadenas si
 * chiffré + barres de RSSI), une ligne de pagination en bas. Toutes les
 * constantes de position dérivent les unes des autres et sont vérifiées par les
 * _Static_assert plus bas -- même discipline que ecran_macros.c/ecran_accueil.c.
 *
 * === Le balayage hors du fil LVGL (le point délicat) ==================
 * wifi_scanner() (wifi.h) est BLOQUANT (plusieurs secondes) et interdit dans un
 * rappel LVGL. Il est donc lancé HORS du fil LVGL et son résultat déposé dans
 * des buffers MODULE-STATIC (g_reseaux/g_nb ci-dessous), JAMAIS dans le contexte
 * d'instance : le contexte est libéré à la destruction de l'écran, alors qu'une
 * tâche de balayage encore en vol continuerait d'écrire -- les statics, eux,
 * survivent à la destruction de l'écran.
 *   - Sur l'appareil (ESP_PLATFORM) : une tâche FreeRTOS one-shot appelle
 *     executer_balayage() puis se supprime (vTaskDelete(NULL)). Elle ne touche
 *     QUE les statics ; si l'écran est détruit pendant qu'elle tourne, elle
 *     termine seule et rien ne pointe vers de la mémoire libérée.
 *   - En host-test / simulateur (pas d'ESP_PLATFORM) : wifi_scanner est un mock
 *     SYNCHRONE ; executer_balayage() est appelée directement et pose g_balayage_fait
 *     tout de suite. Le même code d'affichage marche donc que le balayage soit
 *     instantané (mock) ou lent (matériel).
 * mettre_a_jour() (appelée par la boucle) interroge g_balayage_fait et peuple la
 * liste quand il passe à vrai. L'ordre d'écriture est soigné (données AVANT le
 * drapeau, barrières mémoire) pour que le fil LVGL ne lise jamais une liste
 * à moitié écrite -- voir executer_balayage() et lire_balayage_termine(). */
#include "ecran_reglages_wifi.h"

#include <stdio.h>
#include <string.h>

#include "habillage.h"
#include "clavier.h"
#include "plateforme.h"
#include "wifi.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE 20

#define ENTETE_Y        8
#define ENTETE_HAUTEUR 44
#define RESCAN_LARGEUR 150
#define ENTETE_LARGEUR (LARGEUR_CONTENU - 2 * MARGE - RESCAN_LARGEUR - 12)

#define LISTE_Y       (ENTETE_Y + ENTETE_HAUTEUR + 12)
#define LIGNE_HAUTEUR 56
#define LIGNE_ECART    8
#define LIGNE_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)
#define GRILLE_BAS (LISTE_Y + WIFI_LIGNES_PAR_PAGE * LIGNE_HAUTEUR + \
                    (WIFI_LIGNES_PAR_PAGE - 1) * LIGNE_ECART)

#define PAGINATION_Y       (GRILLE_BAS + 8)
#define PAGINATION_HAUTEUR 44
#define BOUTON_PAGE_LARGEUR 60

/* Cluster de droite dans une ligne (cadenas + barres), en coordonnées locales
 * au bouton de ligne. Barres : 4 rectangles verticaux, mêmes seuils que la
 * barre d'état (plateforme_wifi_barres()). */
#define BARRE_LARGEUR 4
#define BARRE_ECART   3
#define BARRES_TOTAL  (4 * BARRE_LARGEUR + 3 * BARRE_ECART) /* 25 px */
#define BARRES_X      (LIGNE_LARGEUR - 16 - BARRES_TOTAL)
#define CADENAS_LARGEUR 14
#define CADENAS_X      (BARRES_X - 10 - CADENAS_LARGEUR)
#define SSID_X         16
#define SSID_LARGEUR   (CADENAS_X - SSID_X - 12)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_BARRE_ACTIVE     0xE8EDF2 /* meme blanc que le wifi de la barre d'etat (habillage.c) */
#define COULEUR_BARRE_INACTIVE   0x3A4656 /* meme gris que le wifi inactif (habillage.c) */

/* Bande couverte par le bandeau de notification, en coordonnées ABSOLUES
 * d'écran -- mêmes trois valeurs que habillage.c (BARRE_HAUTEUR=44,
 * BANDEAU_HAUTEUR=60, HAUTEUR_ECRAN=480), DOIVENT rester synchronisées avec lui,
 * même convention que ecran_macros.c/ecran_configuration.c. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

_Static_assert(MARGE + ENTETE_LARGEUR + 12 + RESCAN_LARGEUR + MARGE <= LARGEUR_CONTENU,
               "l'en-tete + le bouton Rescan debordent de la largeur du contenu");
_Static_assert(GRILLE_BAS <= PAGINATION_Y,
               "la liste des reseaux chevauche la ligne de pagination");
_Static_assert(PAGINATION_Y + PAGINATION_HAUTEUR <= HAUTEUR_CONTENU,
               "la ligne de pagination deborde de la hauteur du contenu");
/* Même garde-fou que ecran_macros.c : le bas de la ligne de pagination (en
 * coordonnées ABSOLUES d'écran) doit rester strictement au-dessus du haut du
 * bandeau de notification, sinon une notification la recouvrirait. */
_Static_assert(BARRE_HAUTEUR_ECRAN + PAGINATION_Y + PAGINATION_HAUTEUR <= BANDEAU_Y_ECRAN,
               "la ligne de pagination chevauche la bande du bandeau de notification");
_Static_assert(SSID_LARGEUR > 0, "la largeur du SSID est devenue negative");

/* ======================================================================
 * Balayage hors fil LVGL : buffers MODULE-STATIC (survivent à la
 * destruction de l'écran, jamais dans le contexte d'instance).
 * ====================================================================== */
static wifi_reseau_t   g_reseaux[WIFI_RESEAUX_MAX];
static size_t          g_nb;
static volatile bool   g_balayage_fait;    /* le résultat est prêt à être lu */
static volatile bool   g_balayage_en_cours;/* une tâche de balayage est en vol */

/* Exécuté HORS du fil LVGL (tâche FreeRTOS sur l'appareil, appel direct avec le
 * mock synchrone) : fait le balayage bloquant puis publie le résultat. Écrit
 * g_reseaux/g_nb AVANT de poser g_balayage_fait, avec une barrière de
 * publication (release) entre les deux -- lire_balayage_termine() fait la
 * lecture symétrique (acquire). */
static void executer_balayage(void)
{
    wifi_reseau_t tampon[WIFI_RESEAUX_MAX];
    size_t nb = 0;
    esp_err_t err = wifi_scanner(tampon, WIFI_RESEAUX_MAX, &nb);
    if (err != ESP_OK) {
        nb = 0;
    }
    if (nb > WIFI_RESEAUX_MAX) {
        nb = WIFI_RESEAUX_MAX;
    }
    for (size_t i = 0; i < nb; i++) {
        g_reseaux[i] = tampon[i];
    }
    g_nb = nb;
    __atomic_thread_fence(__ATOMIC_RELEASE); /* données publiées AVANT le drapeau */
    g_balayage_fait = true;
    g_balayage_en_cours = false;
}

/* Lecture symétrique côté fil LVGL : rend vrai si un résultat est prêt et,
 * dans ce cas seulement, garantit (barrière acquire) que g_reseaux/g_nb lus
 * ensuite sont bien ceux publiés par executer_balayage(). */
static bool lire_balayage_termine(void)
{
    if (!g_balayage_fait) {
        return false;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return true;
}

#ifdef ESP_PLATFORM
static void tache_balayage(void *arg)
{
    (void)arg;
    executer_balayage();
    vTaskDelete(NULL); /* one-shot : la tâche se supprime elle-même */
}
#endif

/* Lance un balayage s'il n'y en a pas déjà un en vol (jamais deux : évite que
 * deux tâches concurrentes n'entrelacent leurs écritures des statics). Remet
 * g_balayage_fait à faux AVANT d'armer g_balayage_en_cours, pour qu'un ancien
 * résultat d'une instance précédente ne soit pas pris pour celui du balayage
 * qu'on démarre. */
static void demarrer_balayage(void)
{
    if (g_balayage_en_cours) {
        return; /* une tâche tourne déjà ; on récupérera son résultat au poll */
    }
    g_balayage_fait = false;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    g_balayage_en_cours = true;
#ifdef ESP_PLATFORM
    if (xTaskCreate(tache_balayage, "wifi_scan", 4096, NULL, 4, NULL) != pdPASS) {
        /* Impossible de créer la tâche : plutôt que de bloquer le fil LVGL en
         * appelant wifi_scanner() ici, on rend un résultat vide immédiat
         * (« Aucun réseau ») -- l'utilisateur peut réessayer via « Rescan ». */
        g_nb = 0;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        g_balayage_fait = true;
        g_balayage_en_cours = false;
    }
#else
    /* Mock synchrone (host-test / simulateur) : le balayage est instantané ;
     * executer_balayage() pose g_balayage_fait avant de rendre la main. */
    executer_balayage();
#endif
}

/* ======================================================================
 * Widgets
 * ====================================================================== */

static lv_obj_t *bouton_plein(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t largeur,
                               lv_coord_t hauteur)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, largeur, hauteur);
    lv_obj_set_pos(bouton, x, y);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 8, 0);
    return bouton;
}

/* Petit cadenas dessiné (aucune police du projet ne porte de glyphe cadenas,
 * voir la plage 0x20-0x7F des montserrat) : un corps rectangulaire plein
 * surmonté d'une anse en U (rectangle bordé sans remplissage). Rendu
 * déterministe, sans dépendance de police. */
static lv_obj_t *creer_cadenas(lv_obj_t *parent)
{
    lv_obj_t *cadenas = lv_obj_create(parent);
    lv_obj_remove_style_all(cadenas);
    lv_obj_set_size(cadenas, CADENAS_LARGEUR, 16);

    lv_obj_t *anse = lv_obj_create(cadenas);
    lv_obj_remove_style_all(anse);
    lv_obj_set_size(anse, 8, 8);
    lv_obj_set_pos(anse, (CADENAS_LARGEUR - 8) / 2, 0);
    lv_obj_set_style_border_width(anse, 2, 0);
    lv_obj_set_style_border_color(anse, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_radius(anse, 4, 0);

    lv_obj_t *corps = lv_obj_create(cadenas);
    lv_obj_remove_style_all(corps);
    lv_obj_set_size(corps, CADENAS_LARGEUR, 9);
    lv_obj_set_pos(corps, 0, 7);
    lv_obj_set_style_bg_opa(corps, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(corps, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_radius(corps, 2, 0);
    return cadenas;
}

/* Rappel de validation du clavier (mot de passe d'un réseau chiffré) : appelé
 * une fois avec la valeur saisie, ou NULL si annulé (contrat clavier.h). Le
 * SSID vient de ctx->ssid_en_attente, capturé au tap -- jamais d'une saisie
 * utilisateur. */
static void rappel_mot_de_passe(const char *valeur, void *contexte)
{
    ecran_reglages_wifi_ctx_t *ctx = contexte;
    if (ctx == NULL || valeur == NULL) {
        return; /* annulé : rien ne change */
    }
    /* Garde EN_COURS : wifi_reconfigurer() refuserait de toute façon, mais on
     * évite l'appel et on le dit. */
    if (wifi_reconfig_etat() == WIFI_RECONFIG_EN_COURS) {
        habillage_notifier("WiFi busy - reconfiguration in progress", true);
        return;
    }
    esp_err_t err = wifi_reconfigurer(ctx->ssid_en_attente, valeur);
    if (err != ESP_OK) {
        habillage_notifier("Could not apply WiFi settings", true);
    } else {
        habillage_notifier("Connecting...", false);
    }
}

/* Applique un réseau OUVERT (sans mot de passe) directement, ou annonce
 * pourquoi c'est refusé. Facteur commun du chemin « ouvert » (pas de clavier). */
static void reconfigurer_ouvert(const char *ssid)
{
    if (wifi_reconfig_etat() == WIFI_RECONFIG_EN_COURS) {
        habillage_notifier("WiFi busy - reconfiguration in progress", true);
        return;
    }
    esp_err_t err = wifi_reconfigurer(ssid, "");
    if (err != ESP_OK) {
        habillage_notifier("Could not apply WiFi settings", true);
    } else {
        habillage_notifier("Connecting...", false);
    }
}

static void bouton_reseau_cb(lv_event_t *e)
{
    ecran_reglages_wifi_emplacement_t *info = lv_event_get_user_data(e);
    if (info == NULL || info->ctx == NULL) {
        return;
    }
    ecran_reglages_wifi_ctx_t *ctx = info->ctx;
    size_t indice = (size_t)ctx->page * WIFI_LIGNES_PAR_PAGE + info->emplacement;
    if (indice >= ctx->nb) {
        return; /* ligne masquée/vide : ne devrait jamais arriver via un vrai doigt */
    }

    /* Garde EN_COURS AVANT toute action (avant même d'ouvrir un clavier) : tant
     * qu'une reconfiguration est en cours, un tap ne relance rien. */
    if (wifi_reconfig_etat() == WIFI_RECONFIG_EN_COURS) {
        habillage_notifier("WiFi busy - reconfiguration in progress", true);
        return;
    }

    const wifi_reseau_t *reseau = &ctx->reseaux[indice];
    if (reseau->chiffre) {
        /* Réseau chiffré : mot de passe au clavier. Le SSID est capturé ICI
         * (jamais relu depuis un indice au retour du clavier, qui pourrait
         * avoir changé). Clavier TEXTE : un mot de passe WPA n'est pas
         * numérique. Valeur initiale vide -- on ne connaît pas l'ancien mot de
         * passe (wifi.h ne l'expose jamais). */
        snprintf(ctx->ssid_en_attente, sizeof(ctx->ssid_en_attente), "%s", reseau->ssid);
        clavier_ouvrir(reseau->ssid, "", CLAVIER_TEXTE, rappel_mot_de_passe, ctx);
    } else {
        /* Réseau ouvert : aucun mot de passe, reconfiguration directe (choix
         * documenté du brief : pas de confirmation intermédiaire). */
        reconfigurer_ouvert(reseau->ssid);
    }
}

static void bouton_rescanner_cb(lv_event_t *e)
{
    ecran_reglages_wifi_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    ctx->attente_resultat = true;
    demarrer_balayage();
}

static void bouton_precedent_cb(lv_event_t *e);
static void bouton_suivant_cb(lv_event_t *e);
static void afficher_page(ecran_reglages_wifi_ctx_t *ctx);

/* min(1, ceil(nb / WIFI_LIGNES_PAR_PAGE)) : au moins une page, même liste vide. */
static uint8_t nb_pages(size_t nb)
{
    if (nb == 0) {
        return 1;
    }
    return (uint8_t)((nb + WIFI_LIGNES_PAR_PAGE - 1) / WIFI_LIGNES_PAR_PAGE);
}

/* Recopie g_reseaux/g_nb (résultat du balayage) dans le contexte -- appelé une
 * fois par balayage terminé. La copie rend les rappels de tap indépendants d'un
 * rebalayage concurrent qui écraserait les statics. */
static void peupler_depuis_statics(ecran_reglages_wifi_ctx_t *ctx)
{
    size_t nb = g_nb;
    if (nb > WIFI_RESEAUX_MAX) {
        nb = WIFI_RESEAUX_MAX;
    }
    for (size_t i = 0; i < nb; i++) {
        ctx->reseaux[i] = g_reseaux[i];
    }
    ctx->nb = nb;
    if (ctx->page >= nb_pages(ctx->nb)) {
        ctx->page = (uint8_t)(nb_pages(ctx->nb) - 1);
    }
}

/* Barres de RSSI d'une ligne : `barres` (0..4) allumées en blanc, le reste en
 * gris -- même code couleur que la barre d'état. */
static void regler_barres(ecran_reglages_wifi_ctx_t *ctx, uint8_t emplacement, uint8_t barres)
{
    for (uint8_t b = 0; b < 4; b++) {
        lv_color_t couleur = (b < barres) ? lv_color_hex(COULEUR_BARRE_ACTIVE)
                                          : lv_color_hex(COULEUR_BARRE_INACTIVE);
        lv_obj_set_style_bg_color(ctx->barres[emplacement][b], couleur, 0);
    }
}

static void afficher_page(ecran_reglages_wifi_ctx_t *ctx)
{
    bool balayage_en_cours = g_balayage_en_cours || !g_balayage_fait;
    bool vide = (!balayage_en_cours) && (ctx->nb == 0);

    /* Bandeau d'attente / vide, mutuellement exclusifs avec la liste. */
    if (balayage_en_cours) {
        lv_obj_clear_flag(ctx->recherche_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->recherche_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (vide) {
        lv_obj_clear_flag(ctx->vide_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->vide_label, LV_OBJ_FLAG_HIDDEN);
    }

    uint8_t pages = nb_pages(ctx->nb);
    if (ctx->page >= pages) {
        ctx->page = (uint8_t)(pages - 1);
    }

    for (uint8_t emplacement = 0; emplacement < WIFI_LIGNES_PAR_PAGE; emplacement++) {
        size_t indice = (size_t)ctx->page * WIFI_LIGNES_PAR_PAGE + emplacement;
        if (!balayage_en_cours && !vide && indice < ctx->nb) {
            const wifi_reseau_t *reseau = &ctx->reseaux[indice];
            lv_label_set_text(ctx->ssid_labels[emplacement], reseau->ssid);
            if (reseau->chiffre) {
                lv_obj_clear_flag(ctx->cadenas[emplacement], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ctx->cadenas[emplacement], LV_OBJ_FLAG_HIDDEN);
            }
            regler_barres(ctx, emplacement, plateforme_wifi_barres(reseau->rssi));
            lv_obj_clear_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);
        }
    }

    bool page_unique = (pages <= 1) || balayage_en_cours || vide;
    if (page_unique) {
        lv_obj_add_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->page_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->page_label, LV_OBJ_FLAG_HIDDEN);
        char texte[16];
        snprintf(texte, sizeof(texte), "Page %u/%u", (unsigned)(ctx->page + 1), (unsigned)pages);
        lv_label_set_text(ctx->page_label, texte);
    }
}

static void bouton_precedent_cb(lv_event_t *e)
{
    ecran_reglages_wifi_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->page == 0) {
        return;
    }
    ctx->page--;
    afficher_page(ctx);
}

static void bouton_suivant_cb(lv_event_t *e)
{
    ecran_reglages_wifi_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    if ((uint8_t)(ctx->page + 1) >= nb_pages(ctx->nb)) {
        return;
    }
    ctx->page++;
    afficher_page(ctx);
}

/* En-tête d'état : traduit wifi_etat() + wifi_reconfig_etat() en une ligne. Ne
 * dépend JAMAIS de donnees_perimees (l'état Klipper) -- l'état WiFi vient des
 * fonctions wifi_*, pas de la liaison ; le griser sur données Klipper périmées
 * serait mentir (voir le brief). */
static void rafraichir_entete(ecran_reglages_wifi_ctx_t *ctx)
{
    char ssid[33];
    bool connecte = false;
    wifi_etat(ssid, sizeof(ssid), &connecte);

    char texte[80];
    switch (wifi_reconfig_etat()) {
    case WIFI_RECONFIG_EN_COURS:
        snprintf(texte, sizeof(texte), "Connecting...");
        break;
    case WIFI_RECONFIG_REUSSI:
        snprintf(texte, sizeof(texte), "Connected to %s (saved)", ssid);
        break;
    case WIFI_RECONFIG_CONNECTE_NON_PERSISTE:
        snprintf(texte, sizeof(texte), "Connected (not saved)");
        break;
    case WIFI_RECONFIG_ECHOUE:
        snprintf(texte, sizeof(texte), "Failed - check password");
        break;
    case WIFI_RECONFIG_INACTIF:
    default:
        if (connecte && ssid[0] != '\0') {
            snprintf(texte, sizeof(texte), "Connected to %s", ssid);
        } else {
            snprintf(texte, sizeof(texte), "Not connected");
        }
        break;
    }
    lv_label_set_text(ctx->etat_label, texte);
}

static void ecran_reglages_wifi_construire(lv_obj_t *parent, void *contexte)
{
    ecran_reglages_wifi_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* En-tête d'état (gauche). */
    ctx->etat_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->etat_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->etat_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_long_mode(ctx->etat_label, LV_LABEL_LONG_DOT);
    lv_obj_set_size(ctx->etat_label, ENTETE_LARGEUR, ENTETE_HAUTEUR);
    lv_obj_set_pos(ctx->etat_label, MARGE, ENTETE_Y + 8);
    lv_obj_update_layout(ctx->etat_label); /* même piège LV_LABEL_LONG_DOT que ecran_configuration.c */
    lv_label_set_text(ctx->etat_label, "");

    /* Bouton « Rescan » (droite de l'en-tête). */
    ctx->bouton_rescanner =
        bouton_plein(parent, LARGEUR_CONTENU - MARGE - RESCAN_LARGEUR, ENTETE_Y, RESCAN_LARGEUR,
                     ENTETE_HAUTEUR);
    lv_obj_t *label_rescan = lv_label_create(ctx->bouton_rescanner);
    lv_obj_set_style_text_font(label_rescan, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_rescan, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label_rescan, "Rescan");
    lv_obj_center(label_rescan);
    lv_obj_add_event_cb(ctx->bouton_rescanner, bouton_rescanner_cb, LV_EVENT_CLICKED, ctx);

    /* « Searching... » et « No networks » : jamais un écran muet, centrés dans
     * la zone que la liste occuperait. */
    ctx->recherche_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->recherche_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->recherche_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(ctx->recherche_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ctx->recherche_label, "Searching...");
    lv_obj_set_pos(ctx->recherche_label, MARGE, LISTE_Y);
    lv_obj_set_size(ctx->recherche_label, LARGEUR_CONTENU - 2 * MARGE, PAGINATION_Y - LISTE_Y);
    lv_obj_add_flag(ctx->recherche_label, LV_OBJ_FLAG_HIDDEN);

    ctx->vide_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->vide_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->vide_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(ctx->vide_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ctx->vide_label, "No networks - tap Rescan");
    lv_obj_set_pos(ctx->vide_label, MARGE, LISTE_Y);
    lv_obj_set_size(ctx->vide_label, LARGEUR_CONTENU - 2 * MARGE, PAGINATION_Y - LISTE_Y);
    lv_obj_add_flag(ctx->vide_label, LV_OBJ_FLAG_HIDDEN);

    /* Les lignes de la liste (une par emplacement de page). */
    for (uint8_t emplacement = 0; emplacement < WIFI_LIGNES_PAR_PAGE; emplacement++) {
        lv_coord_t y = LISTE_Y + emplacement * (LIGNE_HAUTEUR + LIGNE_ECART);
        ctx->boutons[emplacement] = bouton_plein(parent, MARGE, y, LIGNE_LARGEUR, LIGNE_HAUTEUR);
        lv_obj_add_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);

        ctx->ssid_labels[emplacement] = lv_label_create(ctx->boutons[emplacement]);
        lv_obj_set_style_text_font(ctx->ssid_labels[emplacement], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(ctx->ssid_labels[emplacement],
                                     lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        lv_label_set_long_mode(ctx->ssid_labels[emplacement], LV_LABEL_LONG_DOT);
        lv_obj_set_width(ctx->ssid_labels[emplacement], SSID_LARGEUR);
        lv_obj_align(ctx->ssid_labels[emplacement], LV_ALIGN_LEFT_MID, SSID_X, 0);
        lv_obj_update_layout(ctx->ssid_labels[emplacement]);
        lv_label_set_text(ctx->ssid_labels[emplacement], "");

        ctx->cadenas[emplacement] = creer_cadenas(ctx->boutons[emplacement]);
        lv_obj_align(ctx->cadenas[emplacement], LV_ALIGN_LEFT_MID, CADENAS_X, 0);
        lv_obj_add_flag(ctx->cadenas[emplacement], LV_OBJ_FLAG_HIDDEN);

        static const lv_coord_t hauteurs[4] = { 8, 12, 16, 20 };
        for (uint8_t b = 0; b < 4; b++) {
            lv_obj_t *barre = lv_obj_create(ctx->boutons[emplacement]);
            lv_obj_remove_style_all(barre);
            lv_obj_set_size(barre, BARRE_LARGEUR, hauteurs[b]);
            lv_obj_set_style_bg_opa(barre, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(barre, lv_color_hex(COULEUR_BARRE_INACTIVE), 0);
            lv_obj_set_style_radius(barre, 1, 0);
            /* Barres alignées en bas d'une bande de 22 px centrée verticalement
             * (base commune -> montée en escalier vers la droite). */
            lv_obj_align(barre, LV_ALIGN_LEFT_MID,
                         BARRES_X + b * (BARRE_LARGEUR + BARRE_ECART), (lv_coord_t)((20 - hauteurs[b]) / 2));
            ctx->barres[emplacement][b] = barre;
        }

        ctx->emplacements[emplacement].ctx = ctx;
        ctx->emplacements[emplacement].emplacement = emplacement;
        lv_obj_add_event_cb(ctx->boutons[emplacement], bouton_reseau_cb, LV_EVENT_CLICKED,
                             &ctx->emplacements[emplacement]);
    }

    /* Ligne de pagination (masquée tant qu'une seule page). */
    lv_coord_t centre_x = LARGEUR_CONTENU / 2;
    ctx->bouton_precedent =
        bouton_plein(parent, centre_x - BOUTON_PAGE_LARGEUR - 90, PAGINATION_Y, BOUTON_PAGE_LARGEUR,
                     PAGINATION_HAUTEUR);
    lv_obj_t *label_prec = lv_label_create(ctx->bouton_precedent);
    lv_obj_set_style_text_font(label_prec, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_prec, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label_prec, "<");
    lv_obj_center(label_prec);
    lv_obj_add_event_cb(ctx->bouton_precedent, bouton_precedent_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN);

    ctx->bouton_suivant = bouton_plein(parent, centre_x + 90, PAGINATION_Y, BOUTON_PAGE_LARGEUR,
                                        PAGINATION_HAUTEUR);
    lv_obj_t *label_suiv = lv_label_create(ctx->bouton_suivant);
    lv_obj_set_style_text_font(label_suiv, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_suiv, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label_suiv, ">");
    lv_obj_center(label_suiv);
    lv_obj_add_event_cb(ctx->bouton_suivant, bouton_suivant_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN);

    ctx->page_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->page_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->page_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_size(ctx->page_label, 160, PAGINATION_HAUTEUR);
    lv_obj_set_pos(ctx->page_label, centre_x - 80, PAGINATION_Y);
    lv_obj_set_style_text_align(ctx->page_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ctx->page_label, "");
    lv_obj_add_flag(ctx->page_label, LV_OBJ_FLAG_HIDDEN);

    ctx->nb = 0;
    ctx->page = 0;
    ctx->attente_resultat = true;

    /* Lance le balayage d'ouverture HORS du fil LVGL (voir demarrer_balayage()). */
    demarrer_balayage();

    /* Peint l'état initial (« Searching... » ou, avec le mock synchrone déjà
     * terminé, la liste) sans attendre le premier mettre_a_jour(). */
    rafraichir_entete(ctx);
    afficher_page(ctx);
}

static void ecran_reglages_wifi_mettre_a_jour(const void *etat, bool donnees_perimees,
                                              void *contexte)
{
    ecran_reglages_wifi_ctx_t *ctx = contexte;
    if (ctx == NULL) {
        return;
    }
    /* L'état Klipper et sa péremption ne concernent pas cet écran : l'état WiFi
     * vient des fonctions wifi_*, jamais de la liaison Klipper. */
    (void)etat;
    (void)donnees_perimees;

    /* Poll du balayage : quand un résultat est prêt et que cette instance
     * l'attend, on peuple la liste (une fois). */
    if (ctx->attente_resultat && lire_balayage_termine() && !g_balayage_en_cours) {
        peupler_depuis_statics(ctx);
        ctx->attente_resultat = false;
    }

    rafraichir_entete(ctx);
    afficher_page(ctx);
}

const ecran_desc_t ECRAN_REGLAGES_WIFI = {
    .id = "wifi",
    .titre = "WiFi",
    .taille_contexte = sizeof(ecran_reglages_wifi_ctx_t),
    .construire = ecran_reglages_wifi_construire,
    .mettre_a_jour = ecran_reglages_wifi_mettre_a_jour,
    .detruire = NULL, /* la tâche de balayage n'écrit QUE les statics : rien à libérer ici */
};
