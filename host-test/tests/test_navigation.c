#include <stdbool.h>
#include <string.h>

#include "ecran.h"
#include "navigation.h"
#include "petit_test.h"

/* Écrans jouets : ils comptent leurs appels pour que le test observe le cycle
 * de vie, et n'affichent rien. */
typedef struct { int construits; int maj; int detruits; int dernier_etat; } trace_t;
static trace_t g_trace_a, g_trace_b;

typedef struct { int marqueur; } ctx_a_t;

static void a_construire(lv_obj_t *parent, void *ctx)
{
    (void)parent;
    ctx_a_t *c = ctx;
    /* Prouve que le socle a bien remis le contexte à zéro avant de le confier. */
    if (c->marqueur == 0) g_trace_a.construits++;
    c->marqueur = 0x5A;
}
static void a_maj(const void *etat, void *ctx)
{
    (void)ctx; g_trace_a.maj++; g_trace_a.dernier_etat = *(const int *)etat;
}
static void a_detruire(void *ctx) { (void)ctx; g_trace_a.detruits++; }

static const ecran_desc_t ECRAN_A = {
    .id = "a", .titre = "A", .taille_contexte = sizeof(ctx_a_t),
    .construire = a_construire, .mettre_a_jour = a_maj, .detruire = a_detruire,
};

/* Identique à A, sur g_trace_b, avec .taille_contexte = 0 pour couvrir
 * l'écran sans état : ctx vaut alors toujours NULL, jamais déréférencé. */
static void b_construire(lv_obj_t *parent, void *ctx) { (void)parent; (void)ctx; g_trace_b.construits++; }
static void b_maj(const void *etat, void *ctx)
{
    (void)ctx; g_trace_b.maj++; g_trace_b.dernier_etat = *(const int *)etat;
}
static void b_detruire(void *ctx) { (void)ctx; g_trace_b.detruits++; }

static const ecran_desc_t ECRAN_B = {
    .id = "b", .titre = "B", .taille_contexte = 0,
    .construire = b_construire, .mettre_a_jour = b_maj, .detruire = b_detruire,
};

/* Écran jouet dédié au test de profondeur maximale : sert uniquement à
 * prouver qu'une tentative refusée (pile pleine) ne construit ni ne détruit
 * rien, jamais réellement empilé avec succès. */
typedef struct { int construits; int detruits; } trace_trop_t;
static trace_trop_t g_trace_trop;
static void trop_construire(lv_obj_t *parent, void *ctx) { (void)parent; (void)ctx; g_trace_trop.construits++; }
static void trop_maj(const void *etat, void *ctx) { (void)etat; (void)ctx; }
static void trop_detruire(void *ctx) { (void)ctx; g_trace_trop.detruits++; }

static const ecran_desc_t ECRAN_TROP = {
    .id = "trop", .titre = "Trop", .taille_contexte = 0,
    .construire = trop_construire, .mettre_a_jour = trop_maj, .detruire = trop_detruire,
};

void suite_navigation(void)
{
    printf("suite : navigation\n");
    memset(&g_trace_a, 0, sizeof(g_trace_a));
    memset(&g_trace_b, 0, sizeof(g_trace_b));
    memset(&g_trace_trop, 0, sizeof(g_trace_trop));
    navigation_init(lv_screen_active());

    /* pile vide au depart */ VERIFIER(navigation_profondeur() == 0);
    /* id courant NULL au depart */ VERIFIER(navigation_id_courant() == NULL);

    /* empiler A */ VERIFIER(navigation_empiler(&ECRAN_A) == ESP_OK);
    /* A construit une fois */ VERIFIER(g_trace_a.construits == 1);
    /* profondeur 1 */ VERIFIER(navigation_profondeur() == 1);
    /* titre courant */ VERIFIER(strcmp(navigation_titre_courant(), "A") == 0);

    int etat = 7;
    navigation_mettre_a_jour(&etat);
    /* A recoit la mise a jour */ VERIFIER(g_trace_a.maj == 1);
    /* A recoit le bon etat */ VERIFIER(g_trace_a.dernier_etat == 7);

    /* empiler B */ VERIFIER(navigation_empiler(&ECRAN_B) == ESP_OK);
    /* A n'est PAS detruit sous B */ VERIFIER(g_trace_a.detruits == 0);
    etat = 9;
    navigation_mettre_a_jour(&etat);
    /* Règle de la spécification 5.4 : seul l'écran visible est mis à jour. */
    /* A ne recoit plus rien sous B */ VERIFIER(g_trace_a.maj == 1);
    /* B recoit la mise a jour */ VERIFIER(g_trace_b.maj == 1);

    navigation_depiler();
    /* B detruit au depilement */ VERIFIER(g_trace_b.detruits == 1);
    /* profondeur revenue a 1 */ VERIFIER(navigation_profondeur() == 1);
    etat = 11;
    navigation_mettre_a_jour(&etat);
    /* A recoit a nouveau */ VERIFIER(g_trace_a.maj == 2);
    /* A n'a PAS ete reconstruit */ VERIFIER(g_trace_a.construits == 1);

    navigation_depiler();
    /* depiler le dernier ecran ne le detruit pas */ VERIFIER(g_trace_a.detruits == 0);
    /* profondeur reste 1 */ VERIFIER(navigation_profondeur() == 1);

    /* empiler NULL est refuse */ VERIFIER(navigation_empiler(NULL) == ESP_ERR_INVALID_ARG);
    /* mise a jour sans etat ne plante pas */ VERIFIER((navigation_mettre_a_jour(NULL), true));

    /* Profondeur maximale : la pile contient encore A (profondeur 1) ; la
     * remplir jusqu'à NAVIGATION_PROFONDEUR_MAX puis tenter un empilement de
     * plus doit être refusé sans rien construire ni detruire. */
    while (navigation_profondeur() < NAVIGATION_PROFONDEUR_MAX) {
        VERIFIER(navigation_empiler(&ECRAN_B) == ESP_OK);
    }
    /* pile a profondeur maximale */ VERIFIER(navigation_profondeur() == NAVIGATION_PROFONDEUR_MAX);
    /* empilement en trop refuse */ VERIFIER(navigation_empiler(&ECRAN_TROP) == ESP_ERR_NO_MEM);
    /* rien construit pour l'ecran refuse */ VERIFIER(g_trace_trop.construits == 0);
    /* rien detruit pour l'ecran refuse */ VERIFIER(g_trace_trop.detruits == 0);
    /* profondeur inchangee */ VERIFIER(navigation_profondeur() == NAVIGATION_PROFONDEUR_MAX);
}
