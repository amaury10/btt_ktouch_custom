#include <stdbool.h>
#include <string.h>

#include "ecran.h"
#include "navigation.h"
#include "petit_test.h"

/* Écrans jouets : ils comptent leurs appels pour que le test observe le cycle
 * de vie, et n'affichent rien. `dernier_perimees` trace la dernière valeur
 * de `donnees_perimees` reçue par CE rappel (fix round 1, revue tâche 4) :
 * la seule façon d'observer, sans regarder un pixel, que le champ calculé
 * par habillage_pomper() atteint réellement l'écran visible, et lui seul. */
typedef struct { int construits; int maj; int detruits; int dernier_etat; bool dernier_perimees; } trace_t;
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
static void a_maj(const void *etat, bool donnees_perimees, void *ctx)
{
    (void)ctx;
    g_trace_a.maj++;
    g_trace_a.dernier_etat = *(const int *)etat;
    g_trace_a.dernier_perimees = donnees_perimees;
}
static void a_detruire(void *ctx) { (void)ctx; g_trace_a.detruits++; }

static const ecran_desc_t ECRAN_A = {
    .id = "a", .titre = "A", .taille_contexte = sizeof(ctx_a_t),
    .construire = a_construire, .mettre_a_jour = a_maj, .detruire = a_detruire,
};

/* Identique à A, sur g_trace_b, avec .taille_contexte = 0 pour couvrir
 * l'écran sans état : ctx vaut alors toujours NULL, jamais déréférencé. */
static void b_construire(lv_obj_t *parent, void *ctx) { (void)parent; (void)ctx; g_trace_b.construits++; }
static void b_maj(const void *etat, bool donnees_perimees, void *ctx)
{
    (void)ctx;
    g_trace_b.maj++;
    g_trace_b.dernier_etat = *(const int *)etat;
    g_trace_b.dernier_perimees = donnees_perimees;
}
static void b_detruire(void *ctx) { (void)ctx; g_trace_b.detruits++; }

static const ecran_desc_t ECRAN_B = {
    .id = "b", .titre = "B", .taille_contexte = 0,
    .construire = b_construire, .mettre_a_jour = b_maj, .detruire = b_detruire,
};

/* Écran purement inerte, sans état, sans widget dynamique : les trois
 * rappels sont NULL (voir ecran.h — construire/mettre_a_jour/detruire
 * peuvent tous l'être). Sert de plancher jetable ci-dessous : il permet de
 * dépiler A elle-même sans jamais franchir le garde-fou « dernier écran »,
 * et exerce au passage les trois gardes NULL de navigation.c (aucune ne
 * doit planter). */
static const ecran_desc_t ECRAN_DECOR = {
    .id = "decor", .titre = "Decor", .taille_contexte = 0,
    .construire = NULL, .mettre_a_jour = NULL, .detruire = NULL,
};

/* Compteurs simples (construction/destruction), réutilisés par plusieurs
 * écrans jouets qui n'ont besoin ni d'état ni de suivre une mise à jour. */
typedef struct { int construits; int detruits; } trace_simple_t;

/* Écran jouet dédié au test de profondeur maximale : sert uniquement à
 * prouver qu'une tentative refusée (pile pleine) ne construit ni ne détruit
 * rien, jamais réellement empilé avec succès. */
static trace_simple_t g_trace_trop;
static void trop_construire(lv_obj_t *parent, void *ctx) { (void)parent; (void)ctx; g_trace_trop.construits++; }
static void trop_maj(const void *etat, bool donnees_perimees, void *ctx) { (void)etat; (void)donnees_perimees; (void)ctx; }
static void trop_detruire(void *ctx) { (void)ctx; g_trace_trop.detruits++; }

static const ecran_desc_t ECRAN_TROP = {
    .id = "trop", .titre = "Trop", .taille_contexte = 0,
    .construire = trop_construire, .mettre_a_jour = trop_maj, .detruire = trop_detruire,
};

/* Troisième écran jouet, utilisé uniquement par le test de
 * navigation_accueil() (empilé au sommet, au-dessus de A et B). */
static trace_simple_t g_trace_c;
static void c_construire(lv_obj_t *parent, void *ctx) { (void)parent; (void)ctx; g_trace_c.construits++; }
static void c_detruire(void *ctx) { (void)ctx; g_trace_c.detruits++; }

static const ecran_desc_t ECRAN_C = {
    .id = "c", .titre = "C", .taille_contexte = 0,
    .construire = c_construire, .mettre_a_jour = NULL, .detruire = c_detruire,
};

void suite_navigation(void)
{
    printf("suite : navigation\n");

    /* ------------------------------------------------------------------
     * Preuve DISCRIMINANTE que le contexte est réellement remis à zéro à
     * chaque construction — pas seulement lu à zéro parce qu'une toute
     * première allocation touche une page fraîche de l'OS, ce qui se lit à
     * zéro que le socle utilise calloc() ou malloc() (revue tâche 3,
     * Important 1 : le test précédent ne dépilait jamais A, donc son
     * contexte n'avait jamais servi et ne prouvait rien sur le
     * réemploi). Séquence : empiler DECOR (plancher, pour pouvoir dépiler A
     * elle-même sans franchir le garde-fou « dernier écran »), empiler A
     * (contexte neuf, marqueur à 0 -> construits passe à 1, a_construire
     * pose marqueur = 0x5A), dépiler A (libère ce contexte, qui porte
     * encore 0x5A en mémoire), empiler A UNE SECONDE FOIS : si le socle
     * utilisait malloc() au lieu de calloc(), l'allocateur a de bonnes
     * chances de rendre exactement le même bloc, le plus récemment libéré
     * et de la même taille (comportement de réutilisation immédiate d'un
     * "tcache" glibc pour un petit objet libéré puis aussitôt redemandé) ;
     * a_construire lirait alors marqueur == 0x5A et construits resterait à
     * 1 au lieu de passer à 2. C'est cette divergence-là que ce bloc
     * observe, pas la première construction seule. */
    navigation_init(lv_screen_active());
    memset(&g_trace_a, 0, sizeof(g_trace_a));
    VERIFIER(navigation_empiler(&ECRAN_DECOR) == ESP_OK);
    VERIFIER(navigation_empiler(&ECRAN_A) == ESP_OK);
    /* premiere construction, contexte neuf */ VERIFIER(g_trace_a.construits == 1);
    navigation_depiler();
    /* A detruite, son contexte libere */ VERIFIER(g_trace_a.detruits == 1);
    VERIFIER(navigation_empiler(&ECRAN_A) == ESP_OK);
    /* reconstruite avec un contexte relu a zero, pas le residu 0x5A */
    VERIFIER(g_trace_a.construits == 2);
    navigation_depiler();

    /* ------------------------------------------------------------------
     * Scénario principal (brief de la tâche 3) : cycle de vie normal de la
     * pile, un écran couvert qui ne reçoit plus de mise à jour, le
     * garde-fou du dernier écran, l'empilement refusé. */
    navigation_init(lv_screen_active());
    memset(&g_trace_a, 0, sizeof(g_trace_a));
    memset(&g_trace_b, 0, sizeof(g_trace_b));
    memset(&g_trace_trop, 0, sizeof(g_trace_trop));

    /* pile vide au depart */ VERIFIER(navigation_profondeur() == 0);
    /* id courant NULL au depart */ VERIFIER(navigation_id_courant() == NULL);

    /* empiler A */ VERIFIER(navigation_empiler(&ECRAN_A) == ESP_OK);
    /* A construit une fois */ VERIFIER(g_trace_a.construits == 1);
    /* profondeur 1 */ VERIFIER(navigation_profondeur() == 1);
    /* titre courant */ VERIFIER(strcmp(navigation_titre_courant(), "A") == 0);

    int etat = 7;
    navigation_mettre_a_jour(&etat, false);
    /* A recoit la mise a jour */ VERIFIER(g_trace_a.maj == 1);
    /* A recoit le bon etat */ VERIFIER(g_trace_a.dernier_etat == 7);
    /* A recoit donnees_perimees=false */ VERIFIER(g_trace_a.dernier_perimees == false);

    /* empiler B */ VERIFIER(navigation_empiler(&ECRAN_B) == ESP_OK);
    /* A n'est PAS detruit sous B */ VERIFIER(g_trace_a.detruits == 0);
    etat = 9;
    navigation_mettre_a_jour(&etat, true);
    /* Règle de la spécification 5.4 : seul l'écran visible est mis à jour. */
    /* A ne recoit plus rien sous B */ VERIFIER(g_trace_a.maj == 1);
    /* A garde sa derniere valeur (false), pas mise a jour sous B */
    VERIFIER(g_trace_a.dernier_perimees == false);
    /* B recoit la mise a jour */ VERIFIER(g_trace_b.maj == 1);
    /* B (seul ecran visible) recoit donnees_perimees=true */
    VERIFIER(g_trace_b.dernier_perimees == true);

    navigation_depiler();
    /* B detruit au depilement */ VERIFIER(g_trace_b.detruits == 1);
    /* profondeur revenue a 1 */ VERIFIER(navigation_profondeur() == 1);
    etat = 11;
    navigation_mettre_a_jour(&etat, true);
    /* A recoit a nouveau */ VERIFIER(g_trace_a.maj == 2);
    /* A n'a PAS ete reconstruit */ VERIFIER(g_trace_a.construits == 1);
    /* A redevenue visible recoit desormais donnees_perimees=true */
    VERIFIER(g_trace_a.dernier_perimees == true);

    navigation_depiler();
    /* depiler le dernier ecran ne le detruit pas */ VERIFIER(g_trace_a.detruits == 0);
    /* profondeur reste 1 */ VERIFIER(navigation_profondeur() == 1);

    /* empiler NULL est refuse */ VERIFIER(navigation_empiler(NULL) == ESP_ERR_INVALID_ARG);
    /* mise a jour sans etat ne plante pas */ VERIFIER((navigation_mettre_a_jour(NULL, true), true));

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

    /* ------------------------------------------------------------------
     * navigation_accueil() (revue tâche 3, Important 2 : cette fonction
     * n'avait aucun test). Empile trois écrans (A au fond, B au milieu, C
     * au sommet), appelle navigation_accueil(), et vérifie : la profondeur
     * revient à 1, chaque écran intermédiaire a été détruit exactement une
     * fois, l'écran du fond n'a PAS été reconstruit, et il est redevenu
     * visible (démasqué). Reset complet d'abord : la pile contient encore
     * quatre écrans issus du test de profondeur maximale ci-dessus. */
    navigation_init(lv_screen_active());
    memset(&g_trace_a, 0, sizeof(g_trace_a));
    memset(&g_trace_b, 0, sizeof(g_trace_b));
    memset(&g_trace_c, 0, sizeof(g_trace_c));

    VERIFIER(navigation_empiler(&ECRAN_A) == ESP_OK);
    VERIFIER(navigation_empiler(&ECRAN_B) == ESP_OK);
    VERIFIER(navigation_empiler(&ECRAN_C) == ESP_OK);
    /* trois ecrans empiles */ VERIFIER(navigation_profondeur() == 3);

    /* Garde mettre_a_jour==NULL de navigation.c (navigation_mettre_a_jour(),
     * `if (sommet->desc->mettre_a_jour != NULL)`) : jamais réellement
     * exercée jusqu'ici (revue tâche 3, Minor déféré) — aucun écran à
     * mettre_a_jour NULL n'était au sommet au moment d'un appel. ECRAN_C
     * (mettre_a_jour = NULL) est ici au sommet : cet appel ne doit pas
     * planter, et ne doit toucher ni A ni B (couverts). */
    int etat_sous_c = 42;
    VERIFIER((navigation_mettre_a_jour(&etat_sous_c, false), true));
    /* A (couvert par B puis C) ne recoit rien */ VERIFIER(g_trace_a.maj == 0);
    /* B (couvert par C) ne recoit rien */ VERIFIER(g_trace_b.maj == 0);

    navigation_accueil();

    /* ne garde que le fond */ VERIFIER(navigation_profondeur() == 1);
    /* C (sommet) detruit une fois */ VERIFIER(g_trace_c.detruits == 1);
    /* B (milieu) detruit une fois */ VERIFIER(g_trace_b.detruits == 1);
    /* A (fond) jamais detruit */ VERIFIER(g_trace_a.detruits == 0);
    /* A (fond) pas reconstruit */ VERIFIER(g_trace_a.construits == 1);
    /* le fond redevient l'ecran courant */ VERIFIER(strcmp(navigation_id_courant(), "a") == 0);
    /* le conteneur du fond, premier enfant du parent racine (les ecrans
     * intermediaires ont ete detruits, donc supprimes de cette liste), est
     * redemasque : sans ca, navigation_accueil() laisserait un ecran noir
     * derriere elle malgre une pile qui se dit non vide. */
    lv_obj_t *fond = lv_obj_get_child(lv_screen_active(), 0);
    VERIFIER(fond != NULL);
    VERIFIER(!lv_obj_has_flag(fond, LV_OBJ_FLAG_HIDDEN));
}
