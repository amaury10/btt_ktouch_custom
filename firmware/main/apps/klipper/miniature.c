/* Implémentation du store de miniature -- voir miniature.h pour le contrat
 * complet et le POURQUOI (RAM interne, même leçon que klipper_fichiers.c/
 * power_devices.c ; propriété PSRAM à double sens producteur/consommateur,
 * spécifique à ce store).
 *
 * Verrou : même politique que klipper_fichiers.c/power_devices.c -- un
 * portMUX_TYPE, section critique COURTE réduite à la copie des CHAMPS
 * (jamais un appel bloquant, jamais heap_caps_malloc/free sous le verrou :
 * les libérations réelles se font TOUJOURS hors verrou, voir plus bas). Hors
 * cible (host-test/simulateur, pas d'ESP_PLATFORM), pas de FreeRTOS, le verrou
 * retombe sur un no-op -- même politique que klipper_fichiers.c.
 *
 * Allocateur : PSRAM uniquement côté ESP (`heap_caps_free`, MALLOC_CAP_SPIRAM
 * imposé à l'APPELANT qui alloue -- ce fichier ne fait jamais lui-même
 * `heap_caps_malloc()`, seulement `heap_caps_free()` sur ce que l'appelant
 * lui a confié) ; `free()` standard côté host-test/simulateur (pas de PSRAM
 * sur PC, un test y alloue avec `malloc()` -- voir test_miniature.c).
 *
 * --- Mécanisme de transfert différé (LE point délicat de ce fichier) ------
 *
 * Un tampon PSRAM affiché par l'écran (via un `lv_image_dsc_t` dont
 * `.data` pointe dedans) NE DOIT JAMAIS être libéré pendant que la tâche LVGL
 * pourrait encore le décoder/dessiner -- ces deux tâches (fetch/WS d'un côté,
 * LVGL de l'autre) tournent réellement en parallèle sur cet ESP32-S3 SMP.
 * Libérer depuis le PRODUCTEUR (la tâche qui dépose un nouveau résultat)
 * serait donc une fenêtre de use-after-free bien réelle si le consommateur
 * était au même instant en train d'utiliser l'ancien pointeur qu'il vient de
 * lire via miniature_lire().
 *
 * Solution : `g_a_liberer` est un SEUL slot de tampon "retiré, en attente
 * d'être libéré par le CONSOMMATEUR" (pas une file -- voir plus bas pour le
 * cas où il serait déjà occupé). Un dépôt producteur (poser_prete/_echec/
 * _en_cours/effacer) qui retire l'actif courant le déplace dans ce slot SANS
 * JAMAIS appeler heap_caps_free() dessus. Seul miniature_purger() (appelé
 * PAR LE CONSOMMATEUR, sur la tâche LVGL, en fin de cycle -- voir son
 * contrat) vide ce slot et libère réellement la mémoire. Par construction
 * (voir miniature.h), le tampon qui atterrit dans `g_a_liberer` a
 * NÉCESSAIREMENT déjà été remplacé côté widget par un dépôt PLUS RÉCENT
 * avant que purger() ne soit appelé dans le même cycle -- jamais celui que
 * l'écran vient de poser sur le widget À L'INSTANT.
 *
 * Cas résiduel documenté (comme la fenêtre assumée de moonraker_ws.c,
 * minuterie_reconnexion_cb()) : si un DEUXIÈME dépôt producteur survient
 * AVANT que le consommateur n'ait eu la moindre occasion de purger le
 * premier (`g_a_liberer` déjà occupé), ce fichier libère alors ce premier
 * tampon retiré ICI, sur la tâche productrice -- sûr car un tampon déjà
 * présent dans `g_a_liberer` n'a, par construction, JAMAIS été exposé à
 * miniature_lire() (qui ne rend jamais que `g_donnees`, l'actif COURANT,
 * jamais `g_a_liberer`) : aucun consommateur n'a donc pu en obtenir le
 * pointeur. Structurellement quasi impossible ici (un fetch n'arrive qu'une
 * fois par impression, l'écran se met à jour toutes les 250 ms-1 s -- voir
 * miniature.h) mais géré proprement plutôt que fuité en silence si jamais
 * rencontré. */
#include "miniature.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
#define VERROU_PRENDRE() portENTER_CRITICAL(&g_verrou)
#define VERROU_RENDRE()  portEXIT_CRITICAL(&g_verrou)
#define MINIATURE_FREE(p) heap_caps_free(p)
#else
#include <stdlib.h>
#define VERROU_PRENDRE() ((void)0)
#define VERROU_RENDRE()  ((void)0)
#define MINIATURE_FREE(p) free(p)
#endif

static miniature_etat_t g_etat;
static char             g_fichier[MINIATURE_NOM_MAX];
static uint8_t         *g_donnees;   /* tampon actif (PSRAM), NULL si aucun */
static size_t           g_taille;
static int32_t          g_largeur;
static int32_t          g_hauteur;
static uint32_t         g_generation;
static uint8_t         *g_a_liberer; /* tampon retiré, en attente de miniature_purger() */

/* Retire l'actif courant (sous verrou -- appelée par les quatre fonctions de
 * dépôt ci-dessous, jamais directement). Déplace `g_donnees` vers
 * `g_a_liberer` -- si ce dernier était déjà occupé, voir le commentaire de
 * tête pour pourquoi le libérer ICI (hors verrou, juste après) est sûr dans
 * ce cas précis. Ne touche PAS `g_fichier`/`g_etat`/dimensions : c'est à
 * l'appelant de les poser après ce retrait. */
static void retirer_actif_verrouille(uint8_t **a_liberer_supplementaire_sortie)
{
    *a_liberer_supplementaire_sortie = NULL;
    if (g_a_liberer != NULL) {
        /* Cas résiduel documenté en tête de fichier : deux dépôts sans purge
         * consommateur entre les deux. Rendu à l'appelant pour libération
         * HORS verrou (jamais d'appel bloquant/allocateur sous une section
         * critique portMUX). */
        *a_liberer_supplementaire_sortie = g_a_liberer;
    }
    g_a_liberer = g_donnees;
    g_donnees = NULL;
    g_taille = 0;
    g_largeur = 0;
    g_hauteur = 0;
}

/* Défense en profondeur (pas la voie de rapport d'échec principale -- voir
 * miniature_fetch.c, qui vérifie déjà la signature avant même de lire tout
 * le corps et appelle miniature_poser_echec() explicitement s'il la trouve
 * invalide) : jamais laisser un tampon qui ne COMMENCE même pas par la
 * signature PNG, ou trop court pour être lu sans déborder par le décodeur,
 * atteindre l'état PRÊT -- quel que soit l'appelant présent ou futur. C'est
 * ce qui garantit que `lv_image_decoder_get_info()`/`lv_image_set_src()`
 * (ecran_accueil.c) ne voient jamais des octets sur lesquels le décodeur
 * LODEPNG pourrait déborder.
 *
 * Borne à 24 octets, PAS seulement `sizeof(SIGNATURE)` (8) : lu le code
 * vendorisé de ce dépôt (simulateur/lvgl/src/libs/lodepng/lv_lodepng.c,
 * decoder_info(), branche LV_IMAGE_SRC_VARIABLE) avant d'écrire cette borne
 * -- il ne vérifie QUE `data_size >= 8` (la signature) avant de lire
 * `((uint32_t*)data)[4]` et `[5]` (largeur/hauteur PNG, aux octets 16..23) :
 * un tampon de 8 à 23 octets qui porte la signature mais rien de plus ferait
 * DÉBORDER cette lecture de 16 octets au-delà de son allocation -- un vrai
 * bogue de lv_lodepng.c (bibliothèque tierce vendorisée, pas du code de ce
 * projet, jamais modifiée ici), mais qui deviendrait NOTRE lecture hors
 * bornes dès l'instant où ce store lui tend un tampon trop court. 24 est
 * exactement la taille minimale qui rend cette lecture sûre (signature 8 +
 * longueur de chunk 4 + type de chunk "IHDR" 4 + largeur 4 + hauteur 4) --
 * en dessous, aucun fichier PNG réel ne peut de toute façon exister
 * (l'en-tête IHDR est obligatoire et toujours le premier chunk). */
static bool ressemble_a_un_png(const uint8_t *donnees, size_t taille)
{
    static const uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    static const size_t TAILLE_MIN_SURE = 24;
    return donnees != NULL && taille >= TAILLE_MIN_SURE &&
           memcmp(donnees, SIGNATURE, sizeof(SIGNATURE)) == 0;
}

void miniature_poser_en_cours(const char *fichier)
{
    uint8_t *a_liberer_supplementaire;

    VERROU_PRENDRE();
    retirer_actif_verrouille(&a_liberer_supplementaire);
    snprintf(g_fichier, sizeof(g_fichier), "%s", (fichier != NULL) ? fichier : "");
    g_etat = MINIATURE_EN_COURS;
    g_generation++;
    VERROU_RENDRE();

    if (a_liberer_supplementaire != NULL) {
        MINIATURE_FREE(a_liberer_supplementaire);
    }
}

void miniature_poser_prete(const char *fichier, uint8_t *donnees, size_t taille,
                           int32_t largeur, int32_t hauteur)
{
    if (fichier == NULL || fichier[0] == '\0' || donnees == NULL || taille == 0 ||
        !ressemble_a_un_png(donnees, taille)) {
        /* Défensif : un dépôt PRÊT sans octets, sans fichier, ou dont les
         * octets ne sont manifestement pas un PNG, n'a pas de sens -- si
         * `donnees` a quand même été fourni, on ne le fuit pas. */
        if (donnees != NULL) {
            MINIATURE_FREE(donnees);
        }
        return;
    }

    uint8_t *a_liberer_supplementaire = NULL;
    bool     accepte;

    VERROU_PRENDRE();
    /* Garde de pertinence (voir miniature.h) : rejette un dépôt qui arrive
     * pour un fichier que le store ne suit plus (une impression plus récente
     * a déjà redéclenché miniature_poser_en_cours() pour un AUTRE fichier
     * pendant que ce fetch était en vol). */
    accepte = (strcmp(g_fichier, fichier) == 0);
    if (accepte) {
        retirer_actif_verrouille(&a_liberer_supplementaire);
        g_donnees = donnees;
        g_taille = taille;
        g_largeur = largeur;
        g_hauteur = hauteur;
        g_etat = MINIATURE_PRETE;
        g_generation++;
    }
    VERROU_RENDRE();

    if (!accepte) {
        /* Jamais exposé à quiconque (ni installé dans g_donnees, ni dans
         * g_a_liberer) : sûr de libérer directement ici. */
        MINIATURE_FREE(donnees);
    }
    if (a_liberer_supplementaire != NULL) {
        MINIATURE_FREE(a_liberer_supplementaire);
    }
}

void miniature_poser_echec(const char *fichier)
{
    if (fichier == NULL || fichier[0] == '\0') {
        return;
    }

    uint8_t *a_liberer_supplementaire = NULL;
    bool     accepte;

    VERROU_PRENDRE();
    accepte = (strcmp(g_fichier, fichier) == 0);
    if (accepte) {
        retirer_actif_verrouille(&a_liberer_supplementaire);
        g_etat = MINIATURE_ECHEC;
        g_generation++;
    }
    VERROU_RENDRE();

    if (a_liberer_supplementaire != NULL) {
        MINIATURE_FREE(a_liberer_supplementaire);
    }
}

void miniature_effacer(void)
{
    uint8_t *a_liberer_supplementaire;

    VERROU_PRENDRE();
    retirer_actif_verrouille(&a_liberer_supplementaire);
    g_fichier[0] = '\0';
    g_etat = MINIATURE_ABSENTE;
    g_generation++;
    VERROU_RENDRE();

    if (a_liberer_supplementaire != NULL) {
        MINIATURE_FREE(a_liberer_supplementaire);
    }
}

void miniature_lire(miniature_instantane_t *dest)
{
    if (dest == NULL) {
        return;
    }
    VERROU_PRENDRE();
    dest->etat = g_etat;
    snprintf(dest->fichier, sizeof(dest->fichier), "%s", g_fichier);
    dest->donnees = g_donnees; /* toujours l'ACTIF -- jamais g_a_liberer */
    dest->taille = g_taille;
    dest->largeur = g_largeur;
    dest->hauteur = g_hauteur;
    dest->generation = g_generation;
    VERROU_RENDRE();
}

void miniature_purger(void)
{
    uint8_t *a_liberer;

    VERROU_PRENDRE();
    a_liberer = g_a_liberer;
    g_a_liberer = NULL;
    VERROU_RENDRE();

    if (a_liberer != NULL) {
        MINIATURE_FREE(a_liberer);
    }
}
