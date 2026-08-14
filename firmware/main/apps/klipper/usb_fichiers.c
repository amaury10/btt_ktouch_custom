/* Implémentation de usb_fichiers.h -- voir ce header pour le contrat et le
 * POURQUOI. Verrou : copie EXACTE du patron de klipper_fichiers.c (portMUX,
 * section critique COURTE réduite à la seule copie mémoire, jamais d'appel
 * bloquant sous le verrou ; no-op hors ESP_PLATFORM, mono-thread côté
 * host-test/simulateur).
 *
 * Stockage (g_store, ~4 Ko -- USB_FICHIERS_MAX(32) * sizeof(usb_fichier_t))
 * EN PSRAM côté ESP (fix RAM interne, lot Power/Console/Miniatures/USB : ce
 * store vivait auparavant en .bss RAM interne -- voir la mémoire du projet
 * pour le symptôme, "Error create websocket task"). Même patron que
 * console_log.c : allocation paresseuse via heap_caps_malloc(MALLOC_CAP_SPIRAM)
 * avec garde NULL -- si la PSRAM manque, le store se DÉSACTIVE proprement
 * (no-op sûr), PAS de repli sur une seconde instance statique EN RAM
 * INTERNE (ce qui annulerait le gain de ce fix). Côté host-test/simulateur
 * (pas d'ESP_PLATFORM), une simple instance statique suffit. */
#include "usb_fichiers.h"

#include <stdlib.h>  /* qsort -- usb_listing_trier() */
#include <string.h>
#include <strings.h> /* strcasecmp -- même usage que usb_upload.c */

#include "journal.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
#define VERROU_PRENDRE() portENTER_CRITICAL(&g_verrou)
#define VERROU_RENDRE()  portEXIT_CRITICAL(&g_verrou)

static const char *TAG = "usb_fichiers";

static usb_fichiers_t *g_store; /* PSRAM ; NULL tant que non allouee OU apres un echec definitif */
static bool            g_echec; /* vrai apres un echec d'allocation -- pas de nouvel essai */

/* Même raisonnement que store_obtenir() dans console_log.c : allocation
 * paresseuse HORS verrou ; rend NULL si heap_caps_malloc() échoue -- AUCUN
 * repli sur une seconde instance statique EN RAM INTERNE (annulerait le gain
 * de ce fix), `g_echec` évite seulement de retenter à chaque appel. Les
 * fonctions publiques ci-dessous traitent un NULL comme "store
 * indisponible", jamais un déréférencement. */
static usb_fichiers_t *store_obtenir(void)
{
    if (g_store != NULL) {
        return g_store;
    }
    if (g_echec) {
        return NULL;
    }
    usb_fichiers_t *tampon = (usb_fichiers_t *)heap_caps_malloc(sizeof(usb_fichiers_t), MALLOC_CAP_SPIRAM);
    if (tampon == NULL) {
        JOURNAL_ERREUR(TAG, "heap_caps_malloc(PSRAM) a echoue pour la liste des fichiers USB (%u octets) ; "
                       "store indisponible", (unsigned)sizeof(usb_fichiers_t));
        g_echec = true;
        return NULL;
    }
    memset(tampon, 0, sizeof(*tampon));
    g_store = tampon;
    return g_store;
}
#else
#define VERROU_PRENDRE() ((void)0)
#define VERROU_RENDRE()  ((void)0)
static usb_fichiers_t g_store_hote;
static usb_fichiers_t *store_obtenir(void)
{
    return &g_store_hote;
}
#endif

static uint32_t g_generation;

void usb_fichiers_definir(bool monte, const char *chemin_courant,
                          const usb_fichier_t *fichiers, uint8_t nb, bool tronques)
{
    if (nb > USB_FICHIERS_MAX) {
        nb = USB_FICHIERS_MAX; /* garde défensive, ne devrait jamais arriver depuis usb_scan.c */
    }

    usb_fichiers_t *store = store_obtenir();
    if (store == NULL) {
        return; /* PSRAM indisponible (deja journalise par store_obtenir()) : no-op */
    }

    /* Copie bornée du chemin AVANT le verrou (strlen hors section critique) ;
       clé absente => "" quoi qu'en dise l'appelant : jamais un chemin
       fantôme d'une clé retirée. */
    const char *source_chemin = (monte && chemin_courant != NULL) ? chemin_courant : "";
    size_t longueur_chemin = strlen(source_chemin);
    if (longueur_chemin >= USB_FICHIER_CHEMIN_MAX) {
        longueur_chemin = USB_FICHIER_CHEMIN_MAX - 1;
    }

    VERROU_PRENDRE();
    store->monte = monte;
    memcpy(store->chemin_courant, source_chemin, longueur_chemin);
    store->chemin_courant[longueur_chemin] = '\0';
    store->nb = nb;
    store->tronques = tronques;
    store->scan_en_cours = false; /* toute publication clôt le listage, voir usb_fichiers.h */
    if (nb > 0 && fichiers != NULL) {
        memcpy(store->fichiers, fichiers, (size_t)nb * sizeof(usb_fichier_t));
    }
    /* PAS de remise à zéro de la queue du tableau (revue du 2026-08-15,
       L8) : à 64 entrées, ce memset sous portMUX (interruptions masquées)
       pouvait couvrir ~8,6 Ko -- la section critique doit rester COURTE
       (contrat en tête de fichier). Contrepartie assumée : les emplacements
       au-delà de `nb` peuvent porter des débris d'un listage précédent, tout
       lecteur ne doit regarder que [0..nb) -- ce que usb_fichiers_lire()
       garantit en aval en zéro-remplissant la copie du LECTEUR, hors
       verrou. */
    g_generation++;
    VERROU_RENDRE();
}

const char *usb_chemin_nom(const char *chemin)
{
    if (chemin == NULL) {
        return "";
    }
    const char *slash = strrchr(chemin, '/');
    return (slash != NULL) ? slash + 1 : chemin;
}

void usb_chemin_parent(char *dest, size_t n, const char *chemin)
{
    if (dest == NULL || n == 0) {
        return;
    }
    /* Plancher par défaut : la racine. Tout chemin invalide, hors racine ou
       déjà à la racine y retombe -- jamais un parent au-dessus de /usb. */
    size_t longueur_racine = strlen(USB_RACINE);
    if (chemin == NULL || strncmp(chemin, USB_RACINE, longueur_racine) != 0) {
        snprintf(dest, n, "%s", USB_RACINE);
        return;
    }
    const char *slash = strrchr(chemin, '/');
    size_t longueur_parent = (slash != NULL) ? (size_t)(slash - chemin) : 0;
    if (longueur_parent < longueur_racine) {
        snprintf(dest, n, "%s", USB_RACINE);
        return;
    }
    if (longueur_parent >= n) {
        longueur_parent = n - 1;
    }
    memcpy(dest, chemin, longueur_parent);
    dest[longueur_parent] = '\0';
}

/* Comparateur qsort de usb_listing_trier() -- contrat dans usb_fichiers.h. */
static int usb_listing_comparer(const void *a, const void *b)
{
    const usb_fichier_t *fa = (const usb_fichier_t *)a;
    const usb_fichier_t *fb = (const usb_fichier_t *)b;
    if (fa->est_dossier != fb->est_dossier) {
        return fa->est_dossier ? -1 : 1;
    }
    return strcasecmp(usb_chemin_nom(fa->chemin), usb_chemin_nom(fb->chemin));
}

void usb_listing_trier(usb_fichier_t *entrees, uint8_t nb)
{
    if (entrees == NULL || nb < 2) {
        return;
    }
    qsort(entrees, nb, sizeof(*entrees), usb_listing_comparer);
}

void usb_fichiers_scan_demarre(void)
{
    usb_fichiers_t *store = store_obtenir();
    if (store == NULL) {
        return; /* PSRAM indisponible (deja journalise par store_obtenir()) : no-op */
    }
    VERROU_PRENDRE();
    store->scan_en_cours = true;
    g_generation++;
    VERROU_RENDRE();
}

void usb_fichiers_lire(usb_fichiers_t *dest)
{
    if (dest == NULL) {
        return;
    }
    usb_fichiers_t *store = store_obtenir();
    if (store == NULL) {
        /* PSRAM indisponible : un store vide (clé absente, aucun fichier)
           est la lecture la plus honnête -- jamais de contenu indéterminé. */
        memset(dest, 0, sizeof(*dest));
        return;
    }
    /* Copie sous verrou limitée à l'UTILE (revue du 2026-08-15, L8 : la
       copie du struct entier -- ~8,8 Ko depuis le passage à 64 entrées --
       masquait les interruptions trop longtemps) : en-tête + les `nb`
       entrées réelles. La queue de `dest` est zéro-remplie HORS verrou :
       le lecteur garde l'invariant "au-delà de nb, tout est à zéro" sans
       que le store n'ait à le payer en section critique. */
    uint8_t nb_copie;
    VERROU_PRENDRE();
    dest->monte = store->monte;
    memcpy(dest->chemin_courant, store->chemin_courant, sizeof(dest->chemin_courant));
    dest->tronques = store->tronques;
    dest->scan_en_cours = store->scan_en_cours;
    nb_copie = store->nb;
    if (nb_copie > USB_FICHIERS_MAX) {
        nb_copie = USB_FICHIERS_MAX; /* garde défensive */
    }
    dest->nb = nb_copie;
    if (nb_copie > 0) {
        memcpy(dest->fichiers, store->fichiers, (size_t)nb_copie * sizeof(usb_fichier_t));
    }
    VERROU_RENDRE();
    if (nb_copie < USB_FICHIERS_MAX) {
        memset(&dest->fichiers[nb_copie], 0,
               (size_t)(USB_FICHIERS_MAX - nb_copie) * sizeof(usb_fichier_t));
    }
}

uint32_t usb_fichiers_generation(void)
{
    uint32_t g;
    VERROU_PRENDRE();
    g = g_generation;
    VERROU_RENDRE();
    return g;
}
