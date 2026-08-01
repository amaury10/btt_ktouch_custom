/* Sous-projet 5 (KlipperScreen), tâche 2 : la bascule VIVANTE repos<->impression
 * de l'écran de fond, exercée à travers habillage_pomper() (le vrai chemin
 * d'intégration : chooser injecté -> navigation_remplacer_base()).
 *
 * La primitive navigation_remplacer_base() elle-même est couverte
 * unitairement par test_navigation.c ; ce fichier prouve le SEAM d'intégration
 * de habillage_pomper() : quand un chooser est enregistré, que l'état est
 * disponible et que la pile est à profondeur 1, le fond bascule ; à profondeur
 * > 1 (l'utilisateur dans un sous-écran), il ne bascule jamais.
 *
 * Choix de test DOCUMENTÉ (le brief laissait le choix) : un chooser de TEST
 * déterministe, piloté par un drapeau global (g_bascule_impression), plutôt
 * que d'imposer impression_en_cours au backend factice. Deux raisons :
 *   1. Le seam à prouver est habillage_pomper() -> chooser -> remplacer_base,
 *      pas la valeur exacte que le backend rapporte -- le chooser réel de
 *      l'application (choix_accueil_klipper, app_main.c/simulateur/main.c) et
 *      son helper pur accueil_impression_actif() sont déjà couverts ailleurs
 *      (test_accueil_choix.c + exercés par le simulateur). Un chooser de test
 *      isole ce seam sans dépendre du contenu du backend.
 *   2. Il faut néanmoins que l'état soit DISPONIBLE (ui_etat_instantane() vrai)
 *      pour que le bloc de bascule -- gardé par `disponible` -- tourne : d'où
 *      la réutilisation de la boucle simulée déjà démarrée par
 *      suite_commandes() (singleton process-wide) et un source_etat_sim_cycle()
 *      d'amorce ici.
 *
 * Le chooser rend les VRAIS descripteurs ECRAN_ACCUEIL ("accueil") et
 * ECRAN_ACCUEIL_HUB ("accueil_hub") : les ids vérifiés sont donc ceux que la
 * bascule réelle produira sur cible. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ecran.h"
#include "ecran_accueil.h"
#include "ecran_accueil_hub.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* Drapeau piloté par le test : impression (vrai) => ECRAN_ACCUEIL, repos
 * (faux) => ECRAN_ACCUEIL_HUB. Déterministe, ignore complètement l'état
 * applicatif reçu -- c'est le seam habillage_pomper() -> remplacer_base qu'on
 * prouve, pas le contenu du backend (voir le commentaire de tête). */
static bool g_bascule_impression;

static const ecran_desc_t *choix_bascule_test(const void *etat, void *ctx)
{
    (void)etat;
    (void)ctx;
    return g_bascule_impression ? &ECRAN_ACCUEIL : &ECRAN_ACCUEIL_HUB;
}

/* Sous-écran inerte quelconque empilé par-dessus le fond pour prouver la garde
 * de profondeur (à profondeur 2, aucune bascule). Les trois rappels NULL : il
 * ne sert qu'à occuper le sommet, jamais à afficher quoi que ce soit. */
static const ecran_desc_t ECRAN_SOUS_TEST = {
    .id = "sous-test", .titre = "Sous", .taille_contexte = 0,
    .construire = NULL, .mettre_a_jour = NULL, .detruire = NULL,
};

void suite_bascule_accueil(void)
{
    printf("suite : bascule accueil (habillage_pomper -> remplacer_base)\n");

    /* Mêmes gardes d'ordonnancement que suite_commandes() : ce test réutilise
     * l'habillage construit par suite_ecran_configuration() (habillage_pomper()
     * est un no-op sans lui) ET la boucle simulée démarrée par
     * suite_commandes() (sans elle ui_etat_instantane() rend faux, `disponible`
     * reste faux, la bascule ne tourne jamais). Un message clair plutôt qu'un
     * échec obscur si l'ordre d'enregistrement de tests/main.c changeait. */
    if (!habillage_est_construit()) {
        printf("ERREUR: suite_bascule_accueil() exige un habillage deja construit "
               "(suite_ecran_configuration) -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }
    if (!source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_bascule_accueil() exige la boucle simulee deja demarree "
               "(suite_commandes) -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* Repart d'une pile propre dans lv_screen_active() (même technique de
     * restauration que les sections de test_commandes.c) et amorce un cycle
     * pour garantir un état disponible. */
    navigation_init(lv_screen_active());
    source_etat_sim_cycle();

    /* Précondition explicite : l'état DOIT être disponible, sinon la bascule
     * (gardée par `disponible` dans habillage_pomper()) ne tournerait jamais
     * et les assertions ci-dessous échoueraient sans dire pourquoi. */
    etat_klipper_t e;
    uint32_t gen = 0;
    liaison_etat_t liaison = LIAISON_CONNEXION;
    VERIFIER(ui_etat_instantane(&e, sizeof(e), &gen, &liaison) == true);

    /* Enregistre le chooser de test, fond de départ au repos (accueil_hub). */
    g_bascule_impression = false;
    habillage_definir_choix_accueil(choix_bascule_test, NULL);

    VERIFIER(navigation_empiler(&ECRAN_ACCUEIL_HUB) == ESP_OK);
    /* profondeur 1 */ VERIFIER(navigation_profondeur() == 1);
    /* fond de depart : accueil_hub */ VERIFIER_TEXTE(navigation_id_courant(), "accueil_hub");

    /* Impression : un pompage fait basculer le fond vers "accueil". */
    g_bascule_impression = true;
    habillage_pomper();
    /* profondeur toujours 1 */ VERIFIER(navigation_profondeur() == 1);
    /* le fond a bascule vers accueil */ VERIFIER_TEXTE(navigation_id_courant(), "accueil");

    /* Retour au repos : le fond redevient "accueil_hub". */
    g_bascule_impression = false;
    habillage_pomper();
    /* profondeur toujours 1 */ VERIFIER(navigation_profondeur() == 1);
    /* le fond est redevenu accueil_hub */ VERIFIER_TEXTE(navigation_id_courant(), "accueil_hub");

    /* Idempotence : re-pomper sans changer l'état ne rebascule pas (le fond
     * voulu est déjà en place -- no-op de remplacer_base). */
    habillage_pomper();
    /* toujours accueil_hub */ VERIFIER_TEXTE(navigation_id_courant(), "accueil_hub");
    VERIFIER(navigation_profondeur() == 1);

    /* Garde de profondeur : un sous-écran empilé (profondeur 2) protège le
     * fond -- même en basculant l'état, habillage_pomper() NE change NI le
     * sommet NI le fond NI la profondeur. */
    VERIFIER(navigation_empiler(&ECRAN_SOUS_TEST) == ESP_OK);
    /* profondeur 2 */ VERIFIER(navigation_profondeur() == 2);
    /* sommet : le sous-ecran */ VERIFIER_TEXTE(navigation_id_courant(), "sous-test");

    g_bascule_impression = true; /* l'état bascule vers impression... */
    habillage_pomper();
    /* ...mais a profondeur 2 : rien ne bouge. */
    VERIFIER(navigation_profondeur() == 2);
    /* le sommet reste le sous-ecran */ VERIFIER_TEXTE(navigation_id_courant(), "sous-test");

    /* Et le fond en dessous est resté intact (accueil_hub, jamais basculé vers
     * accueil pendant qu'on était dans le sous-écran) : on dépile pour le
     * vérifier. */
    navigation_depiler();
    /* revenu a profondeur 1 */ VERIFIER(navigation_profondeur() == 1);
    /* le fond preserve est bien accueil_hub */ VERIFIER_TEXTE(navigation_id_courant(), "accueil_hub");

    /* Désenregistre le chooser : les suites suivantes (habillage_pomper() sans
     * bascule attendue) ne doivent pas hériter de celui-ci. Restaure aussi la
     * pile de navigation dans un état neutre. */
    habillage_definir_choix_accueil(NULL, NULL);
    navigation_init(lv_screen_active());
}
