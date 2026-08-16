/* Tests du clavier modal (clavier.h) et du dialogue de confirmation
 * (confirmation.h) — un seul fichier pour les deux, comme test_widgets.c
 * regroupe déjà tuile et progression (tâche 5) : ce sont les deux moitiés
 * du même morceau partagé (spécification §6, voir le brief de la tâche 7).
 *
 * Simulation d'événements : ni l'un ni l'autre widget n'expose ses lv_obj_t*
 * internes (contrat opaque, voir clavier.h/confirmation.h) — les tests les
 * retrouvent par parcours de l'arbre LVGL depuis lv_layer_top(), exactement
 * comme test_navigation.c retrouve le conteneur d'un écran par
 * lv_obj_get_child() plutôt que par un accesseur ajouté pour l'occasion. Pas
 * de pointeur de périphérique tactile simulé : lv_obj_send_event() envoie
 * directement les événements LV_EVENT_READY/CANCEL/CLICKED que lv_keyboard
 * et lv_msgbox enverraient eux-mêmes sur un vrai appui (voir le brief), ce
 * qui suffit puisque clavier.c/confirmation.c ne réagissent qu'à ces
 * événements, jamais à un LV_EVENT_PRESSED brut. */
#include <string.h>

#include "lvgl.h"

#include "clavier.h"
#include "confirmation.h"
#include "ecran.h"
#include "navigation.h"
#include "petit_test.h"

/* Retrouve le premier descendant direct de `parent` dont la classe LVGL est
 * `classe` (comparaison par pointeur de classe, voir lv_obj_check_type()) ;
 * NULL si aucun. Ne descend pas récursivement : la profondeur d'un seul
 * niveau suffit partout où ce fichier l'utilise (voir les commentaires de
 * construction dans clavier.c/confirmation.c pour la structure exacte). */
static lv_obj_t *enfant_de_classe(lv_obj_t *parent, const lv_obj_class_t *classe)
{
    if (parent == NULL) {
        return NULL;
    }
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *enfant = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(enfant, classe)) {
            return enfant;
        }
    }
    return NULL;
}

/* Dernier enfant ajouté à lv_layer_top() : c'est toujours le conteneur
 * plein écran de clavier.c ou le fond de modale de confirmation.c, tant que
 * rien d'autre dans ce binaire de tests n'ajoute d'enfant à la couche
 * supérieure (vrai pour tout le reste du harnais : les autres suites
 * travaillent sous lv_screen_active(), jamais sous lv_layer_top()). */
static lv_obj_t *dernier_enfant_calque_superieur(void)
{
    lv_obj_t *calque = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(calque);
    if (n == 0) {
        return NULL;
    }
    return lv_obj_get_child(calque, n - 1);
}

/* ------------------------------------------------------------------------
 * Section clavier
 * ------------------------------------------------------------------------ */

typedef struct {
    int appels;
    bool dernier_est_null;
    char dernier_valeur[CLAVIER_VALEUR_MAX];
    /* Pointeur BRUT reçu par le rappel (pas une copie) : conservé pour être
     * relu après le pompage qui exécute la destruction asynchrone (voir la
     * propriété 2 ci-dessous) — c'est cette relecture tardive qui prouve que
     * le pointeur ne visait pas la textarea déjà détruite. */
    const char *dernier_pointeur_brut;
} trace_clavier_t;

static trace_clavier_t g_trace_clavier;

static void rappel_clavier(const char *valeur, void *contexte)
{
    (void)contexte;
    g_trace_clavier.appels++;
    g_trace_clavier.dernier_pointeur_brut = valeur;
    if (valeur == NULL) {
        g_trace_clavier.dernier_est_null = true;
        g_trace_clavier.dernier_valeur[0] = '\0';
    } else {
        g_trace_clavier.dernier_est_null = false;
        strncpy(g_trace_clavier.dernier_valeur, valeur, sizeof(g_trace_clavier.dernier_valeur) - 1);
        g_trace_clavier.dernier_valeur[sizeof(g_trace_clavier.dernier_valeur) - 1] = '\0';
    }
}

/* Second rappel, utilisé uniquement pour prouver qu'un second
 * clavier_ouvrir() pendant qu'un premier est ouvert n'est jamais invoqué. */
static int g_appels_rappel_refuse;
static void rappel_refuse(const char *valeur, void *contexte)
{
    (void)valeur;
    (void)contexte;
    g_appels_rappel_refuse++;
}

/* Écran jouet empilé DEPUIS le rappel du clavier (propriété 3) : construire
 * incrémente un compteur et pose un label, prouvant que la construction d'un
 * nouvel écran juste après la fermeture programmée du clavier fonctionne
 * normalement plutôt que de lire une mémoire déjà libérée. */
static int g_ecran_apres_clavier_construits;
static lv_obj_t *g_ecran_apres_clavier_label;

static void ecran_apres_clavier_construire(lv_obj_t *parent, void *ctx)
{
    (void)ctx;
    g_ecran_apres_clavier_construits++;
    g_ecran_apres_clavier_label = lv_label_create(parent);
    lv_label_set_text(g_ecran_apres_clavier_label, "apres-clavier");
}

static const ecran_desc_t ECRAN_APRES_CLAVIER = {
    .id = "apres-clavier", .titre = "Apres clavier", .taille_contexte = 0,
    .construire = ecran_apres_clavier_construire, .mettre_a_jour = NULL, .detruire = NULL,
};

static void rappel_empile_ecran(const char *valeur, void *contexte)
{
    (void)valeur;
    (void)contexte;
    g_trace_clavier.appels++;
    /* Construit un nouvel écran (donc de nouveaux objets LVGL) DEPUIS le
     * rappel, pendant que les objets de l'ancien clavier sont encore vivants
     * (leur destruction n'est que programmée, voir clavier.c) — le mode
     * d'emploi naturel du rappel.
     *
     * Calibrage de ce que ce VERIFIER peut et ne peut PAS détecter (revue
     * tâche 7, round 1 : la version précédente de ce commentaire prétendait
     * qu'ASan détecterait ici une lecture après libération « dans
     * navigation.c » si clavier.c détruisait ses objets de façon
     * synchrone — c'est faux, et l'expérience 1 du rapport de tâche 7 le
     * prouve). `navigation_empiler()` construit sous `lv_screen_active()`,
     * un sous-arbre entièrement DISJOINT de celui du clavier sur
     * `lv_layer_top()` : rien de ce que ce rappel touche ne recoupe jamais
     * la mémoire du clavier, qu'elle soit libérée de façon synchrone ou
     * asynchrone. Ce bloc prouve seulement qu'empiler un écran depuis le
     * rappel produit un widget vivant et correct (voir les VERIFIER juste
     * après l'envoi de l'événement, plus bas) — pas que la destruction du
     * clavier est réellement différée. Cette preuve-là est ailleurs : voir
     * le VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1) juste après
     * l'envoi de LV_EVENT_READY, seul point de ce test où le comportement
     * observable diffère entre une destruction synchrone et asynchrone. */
    VERIFIER(navigation_empiler(&ECRAN_APRES_CLAVIER) == ESP_OK);
}

static void section_clavier(void)
{
    printf("suite : clavier\n");

    /* ------------------------------------------------------------------
     * Propriete 2 (annuler rend NULL) + garde "un seul clavier a la fois".
     * ------------------------------------------------------------------ */
    memset(&g_trace_clavier, 0, sizeof(g_trace_clavier));
    g_appels_rappel_refuse = 0;

    /* rien sur le calque superieur avant toute ouverture */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    clavier_ouvrir("Host", "192.168.1.42", CLAVIER_TEXTE, rappel_clavier, NULL);
    lv_obj_t *racine = dernier_enfant_calque_superieur();
    /* le clavier a bien pose un conteneur plein ecran sur lv_layer_top() */
    VERIFIER(racine != NULL);

    /* Second appel pendant que le premier est ouvert : refuse, aucun second
     * conteneur ajoute, rappel_refuse jamais invoque (meme apres avoir
     * ferme le premier clavier plus bas). */
    clavier_ouvrir("Autre", "", CLAVIER_TEXTE, rappel_refuse, NULL);
    /* toujours un seul conteneur sur le calque superieur */
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1);

    lv_obj_t *kb = enfant_de_classe(racine, &lv_keyboard_class);
    VERIFIER(kb != NULL);
    /* mode texte par defaut demande */ VERIFIER(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_LOWER);

    lv_obj_send_event(kb, LV_EVENT_CANCEL, NULL);
    /* Le clavier est encore VIVANT ici : sa destruction n'est que
     * programmee. Avec lv_obj_delete() synchrone a la place de
     * lv_obj_delete_async() dans clavier.c, ce compteur vaudrait deja 0
     * (verifie : experience 1 du rapport de tache 7, RED provoque puis
     * annule). Volet CANCEL de l'assertion symetrique de la propriete 3
     * plus bas (volet READY). */
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1);
    lv_timer_handler(); /* execute la destruction asynchrone programmee par clavier.c */

    /* Propriete 1 : le rappel a ete appele, une fois exactement. */
    VERIFIER(g_trace_clavier.appels == 1);
    /* Propriete 2 : annuler rend NULL. */
    VERIFIER(g_trace_clavier.dernier_est_null == true);
    /* le clavier refuse pendant l'ouverture du premier n'a jamais ete invoque */
    VERIFIER(g_appels_rappel_refuse == 0);
    /* le conteneur a bien disparu du calque superieur apres le pompage */
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 0);

    /* Un second CANCEL envoye au meme objet (deja detruit de facon
     * asynchrone, mais l'evenement passerait par la meme fonction si elle ne
     * se gardait pas elle-meme) ne doit rien declencher de plus : verifie
     * AVANT le CANCEL ci-dessus qu'un rappel deja ferme reste a 1 aurait ete
     * redondant ici puisque kb n'existe plus reellement une fois le pompage
     * fait ; la garde de reentrance de clavier.c est plutot exercee plus bas
     * (double evenement sur le MEME etat ouvert, sans pompage entre les
     * deux) pour rester dans les limites d'une memoire encore valide. */

    /* ------------------------------------------------------------------
     * Propriete 1, volet "jamais deux" : deux evenements (READY puis
     * CANCEL) livres au meme objet AVANT tout pompage — le second doit
     * heurter la garde de reentrance de clavier.c (g_clavier.ouvert deja
     * remis a false par le premier) plutot que d'invoquer le rappel une
     * deuxieme fois. C'est exactement le "meme si LVGL delivre les deux"
     * du brief.
     * ------------------------------------------------------------------ */
    memset(&g_trace_clavier, 0, sizeof(g_trace_clavier));
    clavier_ouvrir("Host", "abc", CLAVIER_TEXTE, rappel_clavier, NULL);
    racine = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine, &lv_keyboard_class);
    VERIFIER(kb != NULL);

    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    /* deuxieme evenement, delivre au meme objet pas encore reellement detruit */
    lv_obj_send_event(kb, LV_EVENT_CANCEL, NULL);
    lv_timer_handler();

    /* un seul appel malgre les deux evenements livres */
    VERIFIER(g_trace_clavier.appels == 1);
    /* c'est bien le premier (READY -> valeur), pas le second (CANCEL -> NULL) */
    VERIFIER(g_trace_clavier.dernier_est_null == false);
    VERIFIER_TEXTE(g_trace_clavier.dernier_valeur, "abc");

    /* ------------------------------------------------------------------
     * Propriete 2, volet "valider rend la valeur saisie" + survie au-dela
     * de la teardown de la textarea : la valeur est modifiee via l'API
     * lv_textarea_* (equivalent programmatique de la saisie tactile, voir
     * l'en-tete de ce fichier) puis validee.
     * ------------------------------------------------------------------ */
    memset(&g_trace_clavier, 0, sizeof(g_trace_clavier));
    clavier_ouvrir("Gcode", "", CLAVIER_NUMERIQUE, rappel_clavier, NULL);
    racine = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine, &lv_keyboard_class);
    lv_obj_t *ta = enfant_de_classe(racine, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    /* mode numerique bien applique */ VERIFIER(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_NUMBER);

    lv_textarea_set_text(ta, "M117 42");
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    /* Le rappel a deja ete invoque de facon synchrone par lv_obj_send_event
     * ci-dessus (voir clavier.c : le pompage ne fait qu'executer la
     * DESTRUCTION programmee, pas l'appel du rappel lui-meme). */
    VERIFIER(g_trace_clavier.appels == 1);
    VERIFIER(g_trace_clavier.dernier_est_null == false);
    VERIFIER_TEXTE(g_trace_clavier.dernier_valeur, "M117 42");

    lv_timer_handler(); /* detruit reellement la textarea maintenant */

    /* Le pointeur BRUT recu par le rappel, relu APRES que lv_timer_handler()
     * a execute lv_obj_delete_async() sur le conteneur (donc sur `ta`),
     * pointe toujours vers une chaine valide et inchangee : clavier.c l'a
     * pris dans son tampon statique g_clavier.valeur (voir son
     * commentaire), jamais dans lv_textarea_get_text(ta). Ce que cette
     * assertion PEUT detecter : une corruption du contenu ou un crash net
     * si l'implementation rendait par erreur un pointeur dans la textarea
     * (memoire LVGL, allouee par LV_STDLIB_BUILTIN — voir
     * host-test/CMakeLists.txt) ; ce qu'elle NE PEUT PAS detecter : un
     * rapport ASan formel sur cette lecture, puisque LVGL est compilee sans
     * desinfecteurs et que sa memoire leur est invisible (meme avertissement
     * que le brief de cette tache). La garantie vient de l'architecture
     * (tampon statique jamais partage avec un objet LVGL), pas de l'outil. */
    VERIFIER_TEXTE(g_trace_clavier.dernier_pointeur_brut, "M117 42");

    /* ------------------------------------------------------------------
     * Propriete 3 : ouvrir un ecran depuis le rappel ne lit pas de memoire
     * liberee. navigation_init() d'abord, pile fraiche.
     * ------------------------------------------------------------------ */
    navigation_init(lv_screen_active());
    g_ecran_apres_clavier_construits = 0;
    g_ecran_apres_clavier_label = NULL;
    memset(&g_trace_clavier, 0, sizeof(g_trace_clavier));

    clavier_ouvrir("Confirmer", "valeur", CLAVIER_TEXTE, rappel_empile_ecran, NULL);
    racine = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine, &lv_keyboard_class);
    VERIFIER(kb != NULL);

    /* Declenche evenement_clavier() : programme la destruction du clavier
     * PUIS, dans rappel_empile_ecran() ci-dessus, empile un ecran tout neuf
     * (sous-arbre disjoint sous lv_screen_active(), voir son commentaire) —
     * avant que le pompage n'ait seulement eu lieu une fois. */
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);

    /* L'assertion QUI DISCRIMINE reellement une destruction synchrone d'une
     * asynchrone (revue tache 7, round 1 : la version precedente de ce test
     * n'en avait aucune ici — les VERIFIER qui suivent portent tous sur le
     * sous-arbre DISJOINT construit par rappel_empile_ecran(), voir son
     * commentaire, et restent vrais quelle que soit l'implementation de
     * clavier.c). Le clavier est encore VIVANT ici : sa destruction n'est
     * que programmee. Avec lv_obj_delete() synchrone a la place de
     * lv_obj_delete_async() dans clavier.c, ce compteur vaudrait deja 0 —
     * verifie : experience 1 du rapport de tache 7 (RED provoque, colle
     * ci-dessous, puis implementation restauree) :
     *
     *   ECHEC .../test_clavier.c:<cette ligne> :
     *   lv_obj_get_child_count(lv_layer_top()) == 1
     *
     * (lv_obj_get_child_count(lv_layer_top()) valait 0 : le clavier avait
     * deja disparu, detruit de facon synchrone pendant sa propre
     * distribution d'evenement READY, avant meme que rappel_empile_ecran()
     * ne soit appele). */
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1);

    /* le nouvel ecran est construit, avec son widget vivant et correct,
     * AVANT meme tout pompage */
    VERIFIER(g_ecran_apres_clavier_construits == 1);
    VERIFIER(g_ecran_apres_clavier_label != NULL);
    VERIFIER_TEXTE(lv_label_get_text(g_ecran_apres_clavier_label), "apres-clavier");
    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), "apres-clavier");

    lv_timer_handler(); /* execute maintenant la destruction differee de l'ancien clavier */

    /* le nouvel ecran (sous-arbre distinct de l'ancien clavier) est
     * toujours vivant et inchange apres le pompage */
    VERIFIER(g_ecran_apres_clavier_construits == 1);
    VERIFIER_TEXTE(lv_label_get_text(g_ecran_apres_clavier_label), "apres-clavier");
    VERIFIER(navigation_profondeur() == 1);
    /* et le clavier a bien fini par disparaitre du calque superieur */
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 0);

    navigation_depiler(); /* rend le harnais propre pour la suite eventuelle */
}

/* ------------------------------------------------------------------------
 * Section confirmation
 * ------------------------------------------------------------------------ */

typedef struct { int appels; bool dernier_confirme; } trace_confirmation_t;
static trace_confirmation_t g_trace_confirmation;

static void rappel_confirmation(bool confirme, void *contexte)
{
    (void)contexte;
    g_trace_confirmation.appels++;
    g_trace_confirmation.dernier_confirme = confirme;
}

static int g_appels_confirmation_refusee;
static void rappel_confirmation_refusee(bool confirme, void *contexte)
{
    (void)confirme;
    (void)contexte;
    g_appels_confirmation_refusee++;
}

/* Retrouve le pied de la boite retournee par confirmation_ouvrir() : dernier
 * enfant du calque superieur (le fond automatique de lv_msgbox_create(NULL),
 * voir confirmation.c), dont le premier enfant est la boite elle-meme. */
static lv_obj_t *dernier_msgbox(void)
{
    lv_obj_t *fond = dernier_enfant_calque_superieur();
    if (fond == NULL) {
        return NULL;
    }
    return lv_obj_get_child(fond, 0);
}

static void section_confirmation(void)
{
    printf("suite : confirmation\n");

    /* ------------------------------------------------------------------
     * Propriete 1 (une fois, jamais deux) + annuler rend confirme=false.
     * ------------------------------------------------------------------ */
    memset(&g_trace_confirmation, 0, sizeof(g_trace_confirmation));
    g_appels_confirmation_refusee = 0;

    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    confirmation_ouvrir("Annuler l'impression ?", "Cette action est irreversible.",
                         "Cancel print", true, rappel_confirmation, NULL);
    lv_obj_t *mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);

    /* Fond assombri explicitement par confirmation.c, par-dessus le gris
     * eclaircissant que le theme par defaut pose sur msgbox_backdrop_class
     * (voir le commentaire de confirmation_ouvrir() dans confirmation.c) :
     * verifie ici pour qu'un futur ajustement de theme ne puisse pas
     * reprendre la main en silence sur un dialogue destructif. */
    lv_obj_t *fond_modale = dernier_enfant_calque_superieur();
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(fond_modale, 0), lv_color_hex(0x000000)));
    VERIFIER(lv_obj_get_style_bg_opa(fond_modale, 0) == LV_OPA_60);

    /* Second appel pendant que le premier dialogue est ouvert : refuse. */
    confirmation_ouvrir("Autre", "msg", "Action", false, rappel_confirmation_refusee, NULL);
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1);

    lv_obj_t *pied = lv_msgbox_get_footer(mbox);
    VERIFIER(pied != NULL);
    lv_obj_t *bouton_annuler = lv_obj_get_child(pied, 0);
    lv_obj_t *bouton_action  = lv_obj_get_child(pied, 1);
    VERIFIER(bouton_annuler != NULL);
    VERIFIER(bouton_action != NULL);

    /* destructif=true : bouton d'action rouge, PAS l'annulation */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bouton_action, 0), lv_color_hex(0xE74C3C)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(bouton_annuler, 0), lv_color_hex(0xE74C3C)));
    /* et n'est pas le bouton "par defaut" (etat FOCUS_KEY sur l'annulation, jamais sur l'action) */
    VERIFIER(!lv_obj_has_state(bouton_action, LV_STATE_FOCUS_KEY));
    VERIFIER(lv_obj_has_state(bouton_annuler, LV_STATE_FOCUS_KEY));

    /* deux evenements livres au meme bouton avant tout pompage : un seul appel */
    lv_obj_send_event(bouton_annuler, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(bouton_annuler, LV_EVENT_CLICKED, NULL);
    /* Le dialogue est encore VIVANT ici : sa destruction (via
     * lv_msgbox_close_async(), voir confirmation.c) n'est que programmee.
     * Avec lv_msgbox_close() synchrone, ce compteur vaudrait deja 0 — meme
     * methode discriminante que celle du clavier (revue de la tache 7,
     * jalon 2b). */
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1);
    lv_timer_handler();

    VERIFIER(g_trace_confirmation.appels == 1);
    /* annuler rend confirme=false */ VERIFIER(g_trace_confirmation.dernier_confirme == false);
    VERIFIER(g_appels_confirmation_refusee == 0);
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 0);

    /* ------------------------------------------------------------------
     * Le bouton d'action rend confirme=true ; non destructif => pas de
     * rouge, pas de mise en avant forcee de l'annulation.
     * ------------------------------------------------------------------ */
    memset(&g_trace_confirmation, 0, sizeof(g_trace_confirmation));
    confirmation_ouvrir("Wifi", "Se connecter a ce reseau ?", "Connect", false,
                         rappel_confirmation, NULL);
    mbox = dernier_msgbox();
    pied = lv_msgbox_get_footer(mbox);
    bouton_annuler = lv_obj_get_child(pied, 0);
    bouton_action  = lv_obj_get_child(pied, 1);

    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(bouton_action, 0), lv_color_hex(0xE74C3C)));
    VERIFIER(!lv_obj_has_state(bouton_annuler, LV_STATE_FOCUS_KEY));

    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    /* meme assertion discriminante que ci-dessus, chemin "action" cette fois */
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1);
    lv_timer_handler();

    VERIFIER(g_trace_confirmation.appels == 1);
    /* valider rend confirme=true */ VERIFIER(g_trace_confirmation.dernier_confirme == true);
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 0);
}

/* --- dialogue a DEUX actions (gestion de parc, 2026-08-16) --------------- *
 * Le dialogue de confirmation ordinaire n'a que deux issues (action /
 * annulation) ; une tuile du parc en demande trois : editer l'adresse,
 * retirer l'imprimante, ou ne rien faire. */
static struct {
    int appels;
    int dernier_choix;
} g_trace_choix;

static void rappel_choix(int choix, void *contexte)
{
    (void)contexte;
    g_trace_choix.appels++;
    g_trace_choix.dernier_choix = choix;
}

static void section_confirmation_choix(void)
{
    /* Pied a TROIS boutons, dans le meme ordre que le dialogue ordinaire :
       l'annulation en premier, puis les actions dans l'ordre des libelles. */
    memset(&g_trace_choix, 0, sizeof(g_trace_choix));
    confirmation_ouvrir_choix("CR-10 S5", "192.168.1.41:7125",
                              "Edit address", "Remove", NULL, true, rappel_choix, NULL);
    lv_obj_t *mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    lv_obj_t *pied = lv_msgbox_get_footer(mbox);
    VERIFIER(pied != NULL);
    VERIFIER(lv_obj_get_child_count(pied) == 3);

    lv_obj_t *bouton_annuler = lv_obj_get_child(pied, 0);
    lv_obj_t *bouton_a       = lv_obj_get_child(pied, 1);
    lv_obj_t *bouton_b       = lv_obj_get_child(pied, 2);

    /* `destructif_b` ne colore que la SECONDE action et lui refuse la mise en
       avant : "Remove" ne doit jamais etre ce qu'un effleurement declenche. */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bouton_b, 0), lv_color_hex(0xE74C3C)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(bouton_a, 0), lv_color_hex(0xE74C3C)));
    VERIFIER(!lv_obj_has_state(bouton_b, LV_STATE_FOCUS_KEY));

    /* Annulation -> -1. */
    lv_obj_send_event(bouton_annuler, LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    VERIFIER(g_trace_choix.appels == 1);
    VERIFIER(g_trace_choix.dernier_choix == -1);
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 0);

    /* Premiere action -> 0. */
    memset(&g_trace_choix, 0, sizeof(g_trace_choix));
    confirmation_ouvrir_choix("CR-10 S5", "192.168.1.41:7125",
                              "Edit address", "Remove", NULL, true, rappel_choix, NULL);
    pied = lv_msgbox_get_footer(dernier_msgbox());
    lv_obj_send_event(lv_obj_get_child(pied, 1), LV_EVENT_CLICKED, NULL);
    /* Destruction seulement PROGRAMMEE, comme le dialogue ordinaire. */
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1);
    lv_timer_handler();
    VERIFIER(g_trace_choix.appels == 1);
    VERIFIER(g_trace_choix.dernier_choix == 0);

    /* Seconde action -> 1. */
    memset(&g_trace_choix, 0, sizeof(g_trace_choix));
    confirmation_ouvrir_choix("CR-10 S5", "192.168.1.41:7125",
                              "Edit address", "Remove", NULL, true, rappel_choix, NULL);
    pied = lv_msgbox_get_footer(dernier_msgbox());
    lv_obj_send_event(lv_obj_get_child(pied, 2), LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    VERIFIER(g_trace_choix.appels == 1);
    VERIFIER(g_trace_choix.dernier_choix == 1);

    /* Deux clics avant pompage : un seul rappel (meme garde de reentrance que
       le dialogue ordinaire). */
    memset(&g_trace_choix, 0, sizeof(g_trace_choix));
    confirmation_ouvrir_choix("X", "y", "A", "B", NULL, false, rappel_choix, NULL);
    pied = lv_msgbox_get_footer(dernier_msgbox());
    lv_obj_send_event(lv_obj_get_child(pied, 2), LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(lv_obj_get_child(pied, 2), LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    VERIFIER(g_trace_choix.appels == 1);

    /* Un dialogue de choix bloque un second dialogue, comme l'ordinaire. */
    memset(&g_trace_choix, 0, sizeof(g_trace_choix));
    confirmation_ouvrir_choix("X", "y", "A", "B", NULL, false, rappel_choix, NULL);
    confirmation_ouvrir_choix("Z", "w", "C", "D", NULL, false, rappel_choix, NULL);
    VERIFIER(lv_obj_get_child_count(lv_layer_top()) == 1);
    VERIFIER(confirmation_est_ouverte());
    pied = lv_msgbox_get_footer(dernier_msgbox());
    lv_obj_send_event(lv_obj_get_child(pied, 0), LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    VERIFIER(g_trace_choix.appels == 1);
    VERIFIER(!confirmation_est_ouverte());

    /* --- TROIS actions (renommage du parc, 2026-08-16) -------------------
     * `libelle_c` non NUL ajoute un quatrieme bouton. `destructif_dernier`
     * porte alors sur la TROISIEME action, pas la deuxieme : c'est toujours
     * la derniere qui est l'action dangereuse ("Remove"). */
    memset(&g_trace_choix, 0, sizeof(g_trace_choix));
    confirmation_ouvrir_choix("Snapmaker U1", "192.168.1.42:7125",
                              "Rename", "Edit address", "Remove", true, rappel_choix, NULL);
    mbox = dernier_msgbox();
    pied = lv_msgbox_get_footer(mbox);
    VERIFIER(lv_obj_get_child_count(pied) == 4);

    /* La boite s'elargit : les quatre boutons ne tiennent pas dans les 440 px
     * qui suffisent a trois (mesure faite sur docs/captures/parc-actions.png :
     * ~357 px de boutons + ecarts pour trois, ~472 pour quatre). */
    VERIFIER(lv_obj_get_style_width(mbox, 0) > 440);

    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_child(pied, 3), 0),
                         lv_color_hex(0xE74C3C)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_child(pied, 2), 0),
                          lv_color_hex(0xE74C3C)));

    /* Troisieme action -> 2. */
    lv_obj_send_event(lv_obj_get_child(pied, 3), LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    VERIFIER(g_trace_choix.appels == 1);
    VERIFIER(g_trace_choix.dernier_choix == 2);

    /* Premiere action d'un dialogue a trois -> 0 (le renommage). */
    memset(&g_trace_choix, 0, sizeof(g_trace_choix));
    confirmation_ouvrir_choix("Snapmaker U1", "192.168.1.42:7125",
                              "Rename", "Edit address", "Remove", true, rappel_choix, NULL);
    pied = lv_msgbox_get_footer(dernier_msgbox());
    lv_obj_send_event(lv_obj_get_child(pied, 1), LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    VERIFIER(g_trace_choix.dernier_choix == 0);

    /* Deux actions : la boite garde sa largeur d'origine. */
    memset(&g_trace_choix, 0, sizeof(g_trace_choix));
    confirmation_ouvrir_choix("X", "y", "A", "B", NULL, false, rappel_choix, NULL);
    VERIFIER(lv_obj_get_style_width(dernier_msgbox(), 0) == 440);
    pied = lv_msgbox_get_footer(dernier_msgbox());
    lv_obj_send_event(lv_obj_get_child(pied, 0), LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
}

void suite_clavier(void)
{
    section_clavier();
    section_confirmation();
    section_confirmation_choix();
}
