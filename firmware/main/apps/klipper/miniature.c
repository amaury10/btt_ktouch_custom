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
 * Un tampon PSRAM référencé par le widget image de l'écran (via un
 * `lv_image_dsc_t` dont `.data` pointe dedans) NE DOIT JAMAIS être libéré tant
 * que la tâche LVGL peut encore le décoder/dessiner. Point crucial :
 * `lv_image_set_src()` est PARESSEUX -- il mémorise le pointeur et ne lit les
 * octets qu'au DESSIN (lv_timer_handler), sur la tâche LVGL, en parallèle réel
 * de la tâche fetch/WS qui produit les dépôts sur cet ESP32-S3 SMP. Le tampon
 * doit donc rester vivant de set_src jusqu'au prochain rendu ; le libérer
 * depuis le PRODUCTEUR serait un use-after-free bien réel.
 *
 * Règle unique et suffisante : on ne libère JAMAIS le tampon que le
 * CONSOMMATEUR détient. Le consommateur publie ce pointeur dans `g_affiche`,
 * toujours sous le verrou, de DEUX façons complémentaires :
 *   - miniature_lire() pose `g_affiche = g_donnees` : dès l'instant où le
 *     consommateur PREND le pointeur actif pour l'afficher, il est protégé.
 *     C'est ce qui ferme le use-after-free du rapport de revue -- un dépôt
 *     producteur survenant ENTRE le lire et le lv_image_set_src() de l'écran
 *     ne peut plus libérer le tampon que l'écran est justement en train de
 *     poser sur le widget.
 *   - miniature_purger(encore_affiche) pose `g_affiche = encore_affiche` : le
 *     pointeur RÉELLEMENT sur le widget (NULL si l'écran a masqué l'image),
 *     vérité de terrain qui affine la revendication du lire -- un tampon lu
 *     mais finalement NON affiché (dimensions aberrantes, get_info en échec)
 *     redevient ainsi librement libérable.
 *
 * Les DEUX voies de libération consultent `g_affiche` et refusent de free()
 * tout tampon qui lui est égal :
 *   - retirer_actif_verrouille() (tâche PRODUCTRICE) : quand il retire
 *     l'actif, il le DIFFÈRE dans `g_a_liberer` s'il vaut `g_affiche` (détenu
 *     par le consommateur) ; sinon il le rend à libérer HORS verrou (jamais
 *     exposé au consommateur, ou déjà abandonné par lui -- sûr).
 *   - miniature_purger() (tâche LVGL) : ne libère `g_a_liberer` que s'il ne
 *     vaut PLUS `g_affiche`.
 *
 * Un SEUL tampon peut être « détenu » à un instant donné (`g_affiche` est un
 * pointeur unique), donc `g_a_liberer` n'a jamais besoin de plus d'un slot :
 * tout tampon retiré qui n'est PAS le tampon détenu est libéré immédiatement
 * et sûrement (il n'est référencé par aucun widget), seul le tampon détenu
 * transite par le slot en attendant que le consommateur s'en détourne. Ni
 * fuite (le consommateur repasse à chaque cycle et finit par publier un autre
 * `g_affiche`), ni double-free (un tampon quitte `g_a_liberer`/`g_donnees`
 * exactement une fois), ni use-after-free (rien qui vaille `g_affiche` n'est
 * jamais libéré) -- sur fin d'impression, changement de fichier en cours de
 * fetch, deux dépôts rapprochés, ou destruction d'écran. */
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
static const uint8_t   *g_affiche;   /* tampon détenu par le consommateur (sur le widget, ou
                                      * pris via miniature_lire() pour l'être) : JAMAIS libéré
                                      * tant qu'un tampon lui est égal -- voir le commentaire de
                                      * tête (fix use-after-free de la revue opus). */

/* Retire l'actif courant (sous verrou -- appelée par les quatre fonctions de
 * dépôt ci-dessous, jamais directement). NE libère jamais rien sous le verrou
 * (aucun heap_caps_free() en section critique portMUX) : le tampon éventuel à
 * libérer est rendu à l'appelant via `*extra_a_liberer`, pour un free() HORS
 * verrou. Ne touche PAS `g_fichier`/`g_etat`/dimensions : c'est à l'appelant
 * de les poser après ce retrait.
 *
 * Voie de libération PRODUCTRICE (voir le commentaire de tête) : l'actif
 * retiré n'est libérable QUE s'il n'est pas le tampon détenu par le
 * consommateur (`g_affiche`). S'il l'est, on le DIFFÈRE dans l'unique slot
 * `g_a_liberer` (la tâche LVGL peut encore le décoder) ; sinon il est rendu à
 * libérer tout de suite. Comme un seul tampon peut être détenu à la fois, si
 * le slot portait déjà un AUTRE tampon, celui-là n'est plus détenu -- sûr de
 * le rendre à libérer lui aussi (au plus UN free par appel : soit l'actif,
 * soit l'ancien occupant du slot, jamais les deux). */
static void retirer_actif_verrouille(uint8_t **extra_a_liberer)
{
    *extra_a_liberer = NULL;
    uint8_t *actif = g_donnees;
    g_donnees = NULL;
    g_taille = 0;
    g_largeur = 0;
    g_hauteur = 0;
    if (actif == NULL) {
        return;
    }
    if ((const uint8_t *)actif == g_affiche) {
        /* Détenu par le consommateur : INTERDIT de libérer ici. Différer dans
         * le slot ; si le slot portait un autre tampon (plus détenu), le
         * rendre à libérer hors verrou. */
        if (g_a_liberer != NULL && g_a_liberer != actif) {
            *extra_a_liberer = g_a_liberer;
        }
        g_a_liberer = actif;
    } else {
        /* Jamais détenu par le consommateur (jamais lu, ou déjà abandonné) :
         * sûr de le libérer directement (hors verrou). */
        *extra_a_liberer = actif;
    }
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
    /* Revendication (voir le commentaire de tête, fix use-after-free) : dès que
     * le consommateur PREND le pointeur actif pour l'afficher, on le marque
     * détenu -- aucune voie de libération ne le touchera tant qu'il vaut
     * `g_affiche`. Ferme la fenêtre où un dépôt producteur survenant entre ce
     * lire et le lv_image_set_src() de l'écran libérerait ce même tampon. */
    g_affiche = g_donnees;
    VERROU_RENDRE();
}

void miniature_purger(const uint8_t *encore_affiche)
{
    uint8_t *a_liberer = NULL;

    VERROU_PRENDRE();
    /* Vérité de terrain : le pointeur RÉELLEMENT référencé par le widget à cet
     * instant (NULL si l'écran vient de masquer l'image) -- affine la
     * revendication posée par miniature_lire(). */
    g_affiche = encore_affiche;
    /* Ne libère le tampon différé que s'il n'est PLUS détenu par le
     * consommateur (sinon la tâche LVGL peut encore le décoder au dessin). */
    if (g_a_liberer != NULL && (const uint8_t *)g_a_liberer != g_affiche) {
        a_liberer = g_a_liberer;
        g_a_liberer = NULL;
    }
    VERROU_RENDRE();

    if (a_liberer != NULL) {
        MINIATURE_FREE(a_liberer);
    }
}
