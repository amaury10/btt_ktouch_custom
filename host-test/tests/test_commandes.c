/* Tâche 9 : les trois actions (pause/reprise, annulation, arrêt d'urgence),
 * du bouton jusqu'au backend Moonraker.
 *
 * Quatre sections, dans l'ordre où elles s'appuient les unes sur les autres :
 *
 *   1. moonraker_chemin_commande() -- fonction PURE, sans réseau, sans sim.
 *   2. backend_factice_desc()->commande() -- succès / échec forcé / action
 *      inconnue, en appelant le backend directement (même technique que
 *      test_backend_factice.c), toujours sans sim.
 *   3. Les trois boutons de ECRAN_ACCUEIL -- construction directe du contexte
 *      (même technique que test_ecran_accueil.c), événements simulés via
 *      lv_obj_send_event() (même technique que test_clavier.c). A BESOIN de
 *      la boucle simulée démarrée (source_etat_sim_demarrer()) pour que
 *      ui_commander() accepte quoi que ce soit -- démarrée une seule fois ici
 *      pour tout le fichier (source_etat_sim.c est un singleton process-wide,
 *      voir son commentaire de tête).
 *   4. L'échec ASYNCHRONE d'une commande (acceptée par ui_commander(), mais
 *      qui échoue plus tard à l'exécution réelle par la boucle) remonté
 *      jusqu'au bandeau de notification de l'habillage -- réutilise
 *      l'habillage déjà construit par suite_ecran_configuration() (SEULE
 *      suite du harnais à appeler habillage_construire(), voir son propre
 *      commentaire dans test_ecran_configuration.c), donc CE fichier doit
 *      être enregistré APRÈS elle dans tests/main.c pour que ce singleton
 *      existe déjà quand cette section tourne.
 *   5. Un échec d'arrêt d'urgence protégé d'un écrasement par un échec de
 *      pause dans la MÊME rafale de traitement de file (fix round 1, revue
 *      tâche 9, MEDIUM 1).
 *   6. File pleine (fix round 1, revue tâche 9, MEDIUM 2) : ui_commander()
 *      rend ESP_ERR_NO_MEM au bouton, qui le notifie -- chemin SYNCHRONE,
 *      distinct des sections 4/5 ci-dessus. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "backend_factice.h"
#include "ecran_accueil.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "moonraker_parse.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* ------------------------------------------------------------------------
 * Section 1 : moonraker_chemin_commande() -- fonction pure.
 * ------------------------------------------------------------------------ */

static void section_moonraker_chemin_commande(void)
{
    printf("suite : commandes (moonraker_chemin_commande)\n");

    char chemin[MOONRAKER_CHEMIN_COMMANDE_MAX];

    /* pause */
    memset(chemin, 0, sizeof(chemin));
    VERIFIER(moonraker_chemin_commande(BACKEND_ACTION_PAUSE, chemin, sizeof(chemin)) == true);
    VERIFIER_TEXTE(chemin, "printer/print/pause");

    /* reprendre */
    memset(chemin, 0, sizeof(chemin));
    VERIFIER(moonraker_chemin_commande(BACKEND_ACTION_REPRENDRE, chemin, sizeof(chemin)) == true);
    VERIFIER_TEXTE(chemin, "printer/print/resume");

    /* annuler */
    memset(chemin, 0, sizeof(chemin));
    VERIFIER(moonraker_chemin_commande(BACKEND_ACTION_ANNULER, chemin, sizeof(chemin)) == true);
    VERIFIER_TEXTE(chemin, "printer/print/cancel");

    /* arret d'urgence */
    memset(chemin, 0, sizeof(chemin));
    VERIFIER(moonraker_chemin_commande(BACKEND_ACTION_URGENCE, chemin, sizeof(chemin)) == true);
    VERIFIER_TEXTE(chemin, "printer/emergency_stop");

    /* action inconnue : false, `chemin` intact (jamais a moitie rempli). */
    snprintf(chemin, sizeof(chemin), "sentinelle");
    VERIFIER(moonraker_chemin_commande("action_inexistante", chemin, sizeof(chemin)) == false);
    VERIFIER_TEXTE(chemin, "sentinelle");

    /* action NULL : false, sans dereferencer. */
    snprintf(chemin, sizeof(chemin), "sentinelle");
    VERIFIER(moonraker_chemin_commande(NULL, chemin, sizeof(chemin)) == false);
    VERIFIER_TEXTE(chemin, "sentinelle");

    /* `chemin` NULL / `taille` nulle : false, jamais un plantage. */
    VERIFIER(moonraker_chemin_commande(BACKEND_ACTION_PAUSE, NULL, sizeof(chemin)) == false);
    VERIFIER(moonraker_chemin_commande(BACKEND_ACTION_PAUSE, chemin, 0) == false);

    /* Tampon trop court : false, jamais un chemin tronque presente comme
     * valide (re-revue tache 9, round 1 : le correctif existait mais aucun
     * test ne l'epinglait -- un retour a un snprintf sans controle de
     * longueur serait passe inapercu). "printer/print/pause" fait 19
     * caracteres : 8 octets tronquent a coup sur. */
    char court[8];
    VERIFIER(moonraker_chemin_commande(BACKEND_ACTION_PAUSE, court, sizeof(court)) == false);
}

/* ------------------------------------------------------------------------
 * Section 2 : backend_factice_desc()->commande() -- succes / echec force /
 * action inconnue.
 * ------------------------------------------------------------------------ */

static void section_backend_factice_commande(void)
{
    printf("suite : commandes (backend factice)\n");

    const backend_desc_t *d = backend_factice_desc();
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    backend_hote_t hote = { .adresse = "factice", .port = 0 };
    VERIFIER(d->demarrer(&etat, &hote) == ESP_OK);

    /* Succes nominal, action connue -- comportement inchange depuis la
     * tache 8 (voir test_backend_factice.c), reconfirme ici en tete de
     * section pour que le RED eventuel d'un fix round localise clairement le
     * bon fichier. */
    VERIFIER(d->commande(&etat, BACKEND_ACTION_REPRENDRE, NULL) == ESP_OK);

    /* Echec force (tache 9) : une action PAR AILLEURS valide echoue quand
     * meme -- c'est le chemin ASYNCHRONE (commande acceptee en file par
     * ui_commander(), executee plus tard par la boucle, et c'est LA qu'elle
     * echoue) qu'aucun scenario existant du backend factice n'exercait avant
     * cette tache. */
    backend_factice_commande_echoue(true);
    VERIFIER(d->commande(&etat, BACKEND_ACTION_PAUSE, NULL) == ESP_FAIL);
    VERIFIER(d->commande(&etat, BACKEND_ACTION_ANNULER, NULL) == ESP_FAIL);
    VERIFIER(d->commande(&etat, BACKEND_ACTION_URGENCE, NULL) == ESP_FAIL);

    /* Action inconnue : ESP_ERR_NOT_SUPPORTED dans les DEUX cas (echec force
     * ou non), sans effet de bord sur `etat` -- backend_factice_commande()
     * ne le touche jamais, quelle que soit l'action. */
    etat_klipper_t avant = etat;
    VERIFIER(d->commande(&etat, "action_inexistante", NULL) == ESP_ERR_NOT_SUPPORTED);
    VERIFIER(memcmp(&etat, &avant, sizeof(etat)) == 0);

    /* `false` restaure le comportement normal -- verifie explicitement, pas
     * suppose : les suites suivantes (dont la section 3 ci-dessous, et
     * suite_backend_factice() elle-meme si l'ordre venait a changer un jour)
     * comptent sur des commandes connues qui reussissent par defaut. */
    backend_factice_commande_echoue(false);
    VERIFIER(d->commande(&etat, BACKEND_ACTION_PAUSE, NULL) == ESP_OK);

    d->arreter(&etat);
}

/* ------------------------------------------------------------------------
 * Section 3 : les trois boutons de ECRAN_ACCUEIL.
 * ------------------------------------------------------------------------ */

/* Memes helpers de parcours d'arbre que test_clavier.c (section confirmation) :
 * confirmation.h reste opaque sur ses lv_obj_t* internes, chaque fichier de
 * test qui a besoin d'y entrer les retrouve par parcours plutot que par un
 * accesseur ajoute pour l'occasion -- voir le commentaire de tete de
 * test_clavier.c pour la justification complete. Redefinis ici (pas
 * partages) : chaque fichier de test de ce harnais l'a deja fait separement
 * (test_clavier.c, test_ecran_configuration.c), aucune convention commune
 * n'existe pour un utilitaire partage entre fichiers de tests. */
static lv_obj_t *dernier_enfant_calque_superieur(void)
{
    lv_obj_t *calque = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(calque);
    if (n == 0) {
        return NULL;
    }
    return lv_obj_get_child(calque, n - 1);
}

static lv_obj_t *dernier_msgbox(void)
{
    lv_obj_t *fond = dernier_enfant_calque_superieur();
    if (fond == NULL) {
        return NULL;
    }
    return lv_obj_get_child(fond, 0);
}

/* CRITICAL (revue finale jalon 2b) : decouverte en ecrivant le RED de ce
 * fix -- le theme LVGL par defaut (LV_THEME_DEFAULT_TRANSITION_TIME=80,
 * simulateur/lv_conf.h) anime bg_color/text_color sur CHAQUE changement
 * d'etat des boutons, y compris LV_STATE_DISABLED. lv_obj_add_state() pose
 * l'etat de facon synchrone, mais la couleur RESOLUE reste celle de depart
 * (interpolee a t=0) tant que l'animation de transition n'a pas tourne --
 * observe empiriquement (lv_obj_get_local_style_prop() confirmait le bon
 * style local DISABLED enregistre pendant que lv_obj_get_style_bg_color()
 * rendait encore l'ancienne couleur). Aucune autre suite de ce harnais n'a
 * jamais eu besoin de faire avancer le temps LVGL : tuile_griser()/
 * progression_griser() posent leurs styles sur des objets purges du theme
 * via lv_obj_remove_style_all() (voir tuile.c/progression.c), donc sans
 * transition enregistree -- les boutons de ecran_accueil.c, eux, gardent le
 * theme (et sa transition) puisqu'ils passent par lv_button_create() sans
 * jamais l'oter. lv_tick_inc() n'est jamais appele ailleurs dans ce
 * harnais : sans lui lv_tick_get() reste fige et l'animation ne progresse
 * JAMAIS, meme apres un nombre illimite de lv_timer_handler(). 5x100 ms
 * couvre large la duree de transition (80 ms) et sa possible variante
 * retardee (voir trans_delayed/trans_normal dans lv_theme_default.c). */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

static void section_ecran_accueil_boutons(void)
{
    printf("suite : commandes (boutons ecran accueil)\n");

    /* Demarre la boucle simulee UNE SEULE FOIS pour tout ce fichier
     * (singleton process-wide, voir le commentaire de tete) : ui_commander()
     * rend ESP_ERR_INVALID_STATE tant que ceci n'a pas ete fait. */
    VERIFIER(source_etat_sim_demarrer(backend_factice_desc()) == true);
    backend_factice_scenario(1); /* impression en cours, non pertinent ici mais explicite */

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_ctx_t *ctx = (ecran_accueil_ctx_t *)brut;
    ECRAN_ACCUEIL.construire(parent, ctx);
    VERIFIER(ctx->bouton_pause != NULL);
    VERIFIER(ctx->label_pause != NULL);
    VERIFIER(ctx->bouton_annuler != NULL);
    VERIFIER(ctx->bouton_urgence != NULL);

    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.impression_en_cours = true;
    etat.impression_en_pause = false;

    /* --- Pause/Resume : une seule tuile de bouton, deux libelles selon
     * l'etat REEL rapporte par le backend (jamais un etat que le clic
     * anticiperait). */
    ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_pause), "Pause");

    etat.impression_en_pause = true;
    ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_pause), "Resume");

    /* Retour a "printing, non pause" pour le reste de cette section --
     * Pause/Cancel/E-STOP sont tous exerces depuis ce meme etat de depart. */
    etat.impression_en_pause = false;
    ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx);

    /* --- Pause : envoie directement, PAS via confirmation (brief : "pas par
     * confirmation"). Preuve indirecte via la profondeur de la file simulee
     * (source_etat_sim_file_taille(), expose pour les tests uniquement) --
     * aucune fonction publique de source_etat.h ne dit "combien de commandes
     * attendent", seulement "celle-ci a-t-elle ete acceptee". */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_pause, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(dernier_enfant_calque_superieur() == NULL); /* pas de confirmation pour Pause */
    /* MEDIUM 3 (revue tache 9, fix round 1) : le brief est explicite -- ne
     * JAMAIS anticiper l'etat localement pour "faire reactif", un ecran qui
     * anticipe affiche du faux des que la commande echoue. Mutation-prouve
     * par la revue : ajouter un lv_label_set_text(ctx->label_pause, "Resume")
     * dans bouton_pause_cb() gardait la suite a 561/0 sans cette assertion --
     * le libelle doit rester "Pause" jusqu'au PROCHAIN mettre_a_jour(),
     * jamais mis a jour par le clic lui-meme. */
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_pause), "Pause");
    source_etat_sim_cycle(); /* draine avant la suite (echoue jamais ici, commande_echoue=false) */

    /* --- Cancel, decline : ouvre la confirmation, n'empile RIEN tant que
     * l'utilisateur n'a pas confirme. */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_annuler, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(mbox)), "Cancel print?");
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye tant que non confirme */

    lv_obj_t *pied = lv_msgbox_get_footer(mbox);
    lv_obj_t *bouton_decliner = lv_obj_get_child(pied, 0);
    lv_obj_t *bouton_action = lv_obj_get_child(pied, 1);
    VERIFIER(bouton_decliner != NULL);
    VERIFIER(bouton_action != NULL);
    /* destructif=true : bouton d'action rouge, comme l'exige confirmation.h */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bouton_action, 0), lv_color_hex(0xE74C3C)));
    /* LOW (revue tache 9, fix round 1) : le bouton d'action lit deja "Cancel
     * print" -- un declin par defaut "Cancel" ferait deux boutons commencant
     * tous les deux par le meme mot dans le meme dialogue. confirmation_ouvrir_ex()
     * (ecran_accueil.c) lui donne desormais "Keep printing", sans ambiguite
     * sur ce qu'il fait reellement (rien : l'impression continue). */
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton_decliner, 0)), "Keep printing");

    lv_obj_send_event(bouton_decliner, LV_EVENT_CLICKED, NULL);
    lv_timer_handler(); /* acheve la fermeture asynchrone du dialogue */
    VERIFIER(source_etat_sim_file_taille() == avant); /* toujours rien envoye */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    /* --- Cancel, confirme : envoie exactement une commande. */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_annuler, LV_EVENT_CLICKED, NULL);
    mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    pied = lv_msgbox_get_footer(mbox);
    bouton_action = lv_obj_get_child(pied, 1);
    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    /* Le rappel de confirmation.c invoque le rappel de facon SYNCHRONE avant
     * meme de rendre la main a lv_obj_send_event() (voir fermer_et_rappeler()
     * dans confirmation.c) : la commande est deja en file ici, sans besoin de
     * lv_timer_handler() prealable. */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    lv_timer_handler();
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- E-STOP, confirme : meme mecanisme, message et libelle propres.
     * Declin par defaut "Cancel" ici (pas de "Cancel" en double dans le
     * libelle d'action "E-STOP", contrairement au dialogue Cancel print
     * ci-dessus -- confirmation_ouvrir() simple, pas confirmation_ouvrir_ex()). */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_urgence, LV_EVENT_CLICKED, NULL);
    mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(mbox)), "Emergency stop?");
    pied = lv_msgbox_get_footer(mbox);
    bouton_decliner = lv_obj_get_child(pied, 0);
    bouton_action = lv_obj_get_child(pied, 1);
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton_decliner, 0)), "Cancel");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bouton_action, 0), lv_color_hex(0xE74C3C)));
    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    lv_timer_handler();
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- HIGH (revue tache 9, fix round 1) : une confirmation DEJA DONNEE
     * doit partir meme si la liaison s'est degradee PENDANT que le dialogue
     * etait ouvert -- le dialogue reste ouvert le temps qu'un humain lise
     * "This will immediately halt the printer", largement plus que les ~3 s
     * (3 sondages manques) qu'il faut a LIAISON_DEGRADEE pour s'installer.
     * Reproduit exactement le scenario de la revue : ouvre le dialogue AVEC
     * des donnees fraiches, grise la rangee PENDANT qu'il est ouvert (ce
     * qu'habillage_pomper() ferait sur cible entre deux pompes), PUIS
     * confirme. RED contre le code avant ce fix : file inchangee, bandeau
     * intact, dialogue disparu comme si la commande etait partie. */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_urgence, LV_EVENT_CLICKED, NULL);
    mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    pied = lv_msgbox_get_footer(mbox);
    bouton_action = lv_obj_get_child(pied, 1);

    ECRAN_ACCUEIL.mettre_a_jour(&etat, true, ctx); /* la liaison se degrade PENDANT la lecture */
    VERIFIER(lv_obj_has_state(ctx->bouton_urgence, LV_STATE_DISABLED)); /* la rangee EST bien grisee... */
    /* CRITICAL (revue finale jalon 2b) : le drapeau LV_STATE_DISABLED seul ne
     * prouve rien -- c'est exactement le drapeau que le code casse posait
     * deja, pixel-identique a l'etat arme (0/32000 pixels de difference
     * constates par la revue sur la rangee de boutons, E-STOP compris). La
     * couleur RESOLUE doit reellement changer, sinon un lien mort affiche un
     * E-STOP pleinement arme qui ne repond plus au toucher. pomper_transitions_style()
     * termine la transition de theme (voir son commentaire) avant de lire. */
    pomper_transitions_style();
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton_urgence, LV_PART_MAIN), lv_color_hex(0xE74C3C)));

    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL); /* ...mais la confirmation deja ouverte part quand meme */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    lv_timer_handler();
    source_etat_sim_cycle(); /* draine avant la suite */

    /* Retour a des donnees fraiches pour le reste de la section -- pompe
     * AVANT de capturer la reference "active" ci-dessous, sinon la
     * transition de retour depuis l'etat grise du bloc precedent pourrait
     * encore etre en cours et fausser la reference elle-meme. */
    ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx);
    pomper_transitions_style();

    /* Couleurs de reference, boutons actifs -- capturees ICI, sur les memes
     * objets `ctx->bouton_*` qui seront grises puis re-actives ci-dessous,
     * pour prouver un aller-retour complet (meme discipline que
     * tuile_griser()/progression_griser() : donnees_perimees=false doit
     * toujours rendre EXACTEMENT les couleurs normales). */
    lv_color_t fond_pause_actif = lv_obj_get_style_bg_color(ctx->bouton_pause, LV_PART_MAIN);
    lv_color_t fond_annuler_actif = lv_obj_get_style_bg_color(ctx->bouton_annuler, LV_PART_MAIN);
    lv_color_t fond_urgence_actif = lv_obj_get_style_bg_color(ctx->bouton_urgence, LV_PART_MAIN);
    lv_obj_t *label_urgence = lv_obj_get_child(ctx->bouton_urgence, 0);
    lv_color_t texte_urgence_actif = lv_obj_get_style_text_color(label_urgence, LV_PART_MAIN);

    /* --- Grisage sur donnees perimees (revue tache 6) : toute la rangee
     * prend LV_STATE_DISABLED, round-trip complet. */
    ECRAN_ACCUEIL.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_obj_has_state(ctx->bouton_pause, LV_STATE_DISABLED));
    VERIFIER(lv_obj_has_state(ctx->bouton_annuler, LV_STATE_DISABLED));
    VERIFIER(lv_obj_has_state(ctx->bouton_urgence, LV_STATE_DISABLED));
    /* CRITICAL (revue finale jalon 2b) : la couleur RESOLUE (bg des trois
     * boutons, texte du label E-STOP) doit differer de la reference active
     * capturee juste au-dessus -- lv_obj_get_style_*_color() resout pour
     * l'etat COURANT de l'objet, mais seulement UNE FOIS la transition de
     * theme terminee (voir pomper_transitions_style() ci-dessus : sans elle
     * la couleur resolue reste celle de depart malgre lv_obj_add_state()
     * deja applique de facon synchrone). Un simple lv_obj_has_state() ne
     * l'aurait jamais attrape : c'est le drapeau que le code casse posait
     * deja, sans qu'aucun pixel ne bouge. */
    pomper_transitions_style();
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton_pause, LV_PART_MAIN), fond_pause_actif));
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton_annuler, LV_PART_MAIN), fond_annuler_actif));
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton_urgence, LV_PART_MAIN), fond_urgence_actif));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(label_urgence, LV_PART_MAIN), texte_urgence_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(label_urgence, LV_PART_MAIN), lv_color_hex(0x6B7280)));

    /* Un clic direct (lv_obj_send_event() ne passe jamais par la
     * verification tactile de lv_indev.c sur LV_STATE_DISABLED, voir le
     * commentaire de bouton_pause_cb() dans ecran_accueil.c) ne doit rien
     * envoyer tant que les donnees restent perimees -- c'est la garde
     * defensive de ecran_accueil.c, pas LVGL, qui est ici mise a l'epreuve. */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_pause, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    lv_obj_send_event(ctx->bouton_annuler, LV_EVENT_CLICKED, NULL);
    VERIFIER(dernier_enfant_calque_superieur() == NULL); /* pas de confirmation non plus */

    /* Redevient normal : re-active, un clic fonctionne a nouveau. */
    ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_state(ctx->bouton_pause, LV_STATE_DISABLED));
    VERIFIER(!lv_obj_has_state(ctx->bouton_annuler, LV_STATE_DISABLED));
    VERIFIER(!lv_obj_has_state(ctx->bouton_urgence, LV_STATE_DISABLED));
    /* La couleur resolue revient EXACTEMENT a la reference active -- pas
     * seulement "differente du gris", un grisage a sens unique serait tout
     * aussi trompeur (lecon de la revue tache 4, deja citee dans
     * tuile_griser()). pomper_transitions_style() termine a nouveau la
     * transition de retour avant de lire. */
    pomper_transitions_style();
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton_pause, LV_PART_MAIN), fond_pause_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton_annuler, LV_PART_MAIN), fond_annuler_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton_urgence, LV_PART_MAIN), fond_urgence_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(label_urgence, LV_PART_MAIN), texte_urgence_actif));

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_pause, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    source_etat_sim_cycle(); /* draine avant la section suivante */

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Section 4 : echec ASYNCHRONE d'une commande -- remontee jusqu'au bandeau
 * de notification de l'habillage.
 * ------------------------------------------------------------------------ */

static void section_echec_asynchrone(void)
{
    printf("suite : commandes (echec asynchrone -> notification)\n");

    /* Reutilise l'habillage deja construit par suite_ecran_configuration()
     * (singleton, voir le commentaire de tete de ce fichier) : le bandeau de
     * notification est le DERNIER enfant de lv_screen_active() a CONDITION
     * que rien d'autre n'ait ete empile directement dessus depuis. Ce n'est
     * PLUS vrai en sortant de suite_ecran_configuration() : sa derniere
     * section (section_topologie_reelle()) a appele navigation_init() sur
     * lv_screen_active() DIRECTEMENT (pas sur g_contenu, le sous-conteneur
     * que habillage_construire() lui reserve normalement) et y a laisse
     * ECRAN_ACCUEIL empile par-dessus -- un ecran de PLUS, ajoute apres
     * g_bandeau, qui deviendrait donc a tort le "dernier enfant" trouve ici
     * (RED reellement observe en ecrivant ce test : lv_label_get_text() sur
     * un objet qui n'est pas un label plante -- revue de la tache 9, jalon 2b).
     * navigation_init(lv_screen_active()) ICI detruit cet ecran residuel
     * (navigation.c ne detruit que ce qu'IL a lui-meme empile, jamais
     * g_barre/g_contenu/g_bandeau, crees avant tout appel a navigation_init()
     * -- voir habillage_construire()) et restaure g_bandeau comme dernier
     * enfant, exactement comme juste apres habillage_construire(). Meme
     * technique que section_topologie_reelle() elle-meme documente comme
     * sure et repetable. */
    navigation_init(lv_screen_active());

    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *bandeau = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(bandeau != NULL);
    lv_obj_t *bandeau_texte = lv_obj_get_child(bandeau, 0);
    VERIFIER(bandeau_texte != NULL);

    backend_factice_commande_echoue(true);
    VERIFIER(ui_commander(BACKEND_ACTION_PAUSE, NULL) == ESP_OK); /* acceptee en file... */
    source_etat_sim_cycle();                                      /* ...executee ici... */
    /* ...et c'est SEULEMENT maintenant, apres coup, qu'elle echoue -- le
     * rappel de bouton d'origine (s'il y en avait eu un) a deja rendu la main
     * depuis longtemps, sans jamais connaitre ce resultat. */
    habillage_pomper(); /* remonte l'echec au bandeau via ui_commande_echec() */

    VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Command failed: pause");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C)));

    /* Consommation unique (voir ui_commande_echec() dans source_etat.h) : un
     * second pompage sans nouvel echec ne re-notifie rien -- verifie en
     * changeant le texte du bandeau directement, puis en prouvant qu'un
     * pompage supplementaire le laisse intact. */
    habillage_notifier("sentinelle", false);
    habillage_pomper();
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "sentinelle");

    /* Restaure le comportement normal du backend factice : hygiene de fin de
     * section, au cas ou une section suivante compterait dessus. */
    backend_factice_commande_echoue(false);
}

/* ------------------------------------------------------------------------
 * Section 5 : un echec d'arret d'urgence protege d'un ecrasement par un
 * echec d'une AUTRE action dans la MEME rafale de traitement de file
 * (fix round 1, revue tache 9, MEDIUM 1).
 * ------------------------------------------------------------------------ */

static void section_echec_urgence_priorite(void)
{
    printf("suite : commandes (echec urgence protege d'un ecrasement)\n");

    /* Meme technique de restauration que section_echec_asynchrone() ci-dessus
     * (voir son commentaire) : remet le bandeau en dernier enfant de
     * lv_screen_active() avant de le chercher. Idempotent si l'arbre est deja
     * dans cet etat (rien empile depuis la section precedente). */
    navigation_init(lv_screen_active());
    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *bandeau = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(bandeau != NULL);
    lv_obj_t *bandeau_texte = lv_obj_get_child(bandeau, 0);
    VERIFIER(bandeau_texte != NULL);

    /* MEDIUM 1 (revue tache 9, fix round 1) : deux commandes en file, toutes
     * deux vouees a l'echec, drainees en UNE SEULE rafale par
     * source_etat_sim_cycle() (traiter_commandes() depile tout ce qui est en
     * file sans jamais rendre la main entre deux commandes -- rien n'oblige
     * un pompage entre elles). Sans la protection ajoutee par ce fix,
     * l'echec de l'arret d'urgence (traite EN PREMIER, voir l'ordre FIFO de
     * la file) serait ecrase par celui de la pause qui le suit dans la MEME
     * rafale -- exactement le scenario reproduit par la revue. */
    backend_factice_commande_echoue(true);
    VERIFIER(ui_commander(BACKEND_ACTION_URGENCE, NULL) == ESP_OK);
    VERIFIER(ui_commander(BACKEND_ACTION_PAUSE, NULL) == ESP_OK);
    source_etat_sim_cycle(); /* draine les DEUX dans la MEME rafale -- les deux echouent */
    habillage_pomper();      /* ne doit remonter QUE l'echec de l'arret d'urgence */

    VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Command failed: emergency stop");

    backend_factice_commande_echoue(false);
}

/* ------------------------------------------------------------------------
 * Section 6 : file pleine -- chemin SYNCHRONE (ui_commander() rend
 * ESP_ERR_NO_MEM tout de suite au clic), distinct de l'echec ASYNCHRONE des
 * sections 4/5 ci-dessus (fix round 1, revue tache 9, MEDIUM 2).
 * ------------------------------------------------------------------------ */

static void section_file_pleine(void)
{
    printf("suite : commandes (file pleine -> notification)\n");

    navigation_init(lv_screen_active()); /* meme restauration que les sections 4/5 */
    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *bandeau = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(bandeau != NULL);
    lv_obj_t *bandeau_texte = lv_obj_get_child(bandeau, 0);
    VERIFIER(bandeau_texte != NULL);

    /* Texte de depart bien distinct de celui attendu en fin de section : les
     * assertions finales ne passeraient pas "par hasard" si rien n'ecrivait
     * jamais dans le bandeau (meme discipline que le RED provoque par la
     * revue : mutation-prouve, executer_commande() vide de son
     * habillage_notifier() gardait la suite a 561/0 sans un test comme
     * celui-ci). */
    habillage_notifier("sentinelle-file-pleine", false);

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_ctx_t *ctx = (ecran_accueil_ctx_t *)brut;
    ECRAN_ACCUEIL.construire(parent, ctx);

    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.impression_en_cours = true;
    ECRAN_ACCUEIL.mettre_a_jour(&etat, false, ctx);

    /* MEDIUM 2 : remplit la file (profondeur 4, FILE_PROFONDEUR dans
     * source_etat_sim.c) directement via ui_commander(), puis un clic REEL
     * sur Pause pour le 5eme -- lui seul doit rendre ESP_ERR_NO_MEM. */
    VERIFIER(source_etat_sim_file_taille() == 0);
    for (int i = 0; i < 4; i++) {
        VERIFIER(ui_commander(BACKEND_ACTION_PAUSE, NULL) == ESP_OK);
    }
    VERIFIER(source_etat_sim_file_taille() == 4);

    lv_obj_send_event(ctx->bouton_pause, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == 4); /* inchangee : le 5eme a ete refuse */
    VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Command failed: pause");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C)));

    source_etat_sim_cycle(); /* draine les 4 commandes reellement en file (aucune n'echoue ici) */

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Section 7 (defaut 1, revue live jalon 3a) : un ecran empile PENDANT une
 * periode calme (aucun nouveau cycle de la boucle entre l'empilement et le
 * pompage suivant, donc generation ET liaison INCHANGEES) doit quand meme
 * recevoir un premier mettre_a_jour -- PAS seulement quand generation ou
 * liaison changent.
 *
 * Root cause confirmee (pas re-derivee ici) : habillage_pomper()
 * (ui/habillage.c) ne relayait navigation_mettre_a_jour() que si
 * `generation != g_derniere_generation || liaison != g_derniere_liaison`.
 * Un ecran fraichement empile (navigation_empiler(), ui/navigation.c) voit
 * son `construire` tourner avec un contexte vide, mais son PREMIER
 * `mettre_a_jour` n'arrivait qu'au prochain changement reel de generation ou
 * de liaison -- qui peut ne jamais survenir pendant une periode calme (sur
 * vkp, les temperatures sont constantes : generation avance a peine). Tout
 * ecran empile pendant une telle periode reste donc vide (ex. ECRAN_MACROS,
 * qui affichait "No macros" malgre nb_macros>0) jusqu'a ce que quelque chose
 * d'AUTRE bouge dans l'etat applicatif. */

typedef struct { int maj; } trace_ecran_calme_t;
static trace_ecran_calme_t g_trace_ecran_calme;

static void ecran_calme_maj(const void *etat, bool donnees_perimees, void *ctx)
{
    (void)etat;
    (void)donnees_perimees;
    (void)ctx;
    g_trace_ecran_calme.maj++;
}

static const ecran_desc_t ECRAN_JOUET_CALME = {
    .id = "calme", .titre = "Calme", .taille_contexte = 0,
    .construire = NULL, .mettre_a_jour = ecran_calme_maj, .detruire = NULL,
};

static void section_ecran_pousse_pendant_periode_calme(void)
{
    printf("suite : commandes (ecran pousse pendant periode calme -> maj immediate)\n");

    /* Meme restauration que les sections precedentes (navigation_init() sur
     * lv_screen_active() directement) : ce test n'a besoin que de la pile de
     * navigation, pas du reste de l'habillage. */
    navigation_init(lv_screen_active());
    memset(&g_trace_ecran_calme, 0, sizeof(g_trace_ecran_calme));

    /* Amorce : aligne les tampons "dernier generation/liaison" statiques de
     * habillage.c sur l'etat courant AVANT d'empiler l'ecran jouet -- SANS
     * source_etat_sim_cycle() ici, donc sans toucher la generation. La pile
     * est vide a cet instant (navigation_mettre_a_jour() ne fait rien sur
     * profondeur==0), cette pompe n'a donc aucun effet sur
     * ECRAN_JOUET_CALME lui-meme. */
    habillage_pomper();

    VERIFIER(navigation_empiler(&ECRAN_JOUET_CALME) == ESP_OK);
    /* construire == NULL : la preuve isole mettre_a_jour, sans effet de bord
     * possible d'une construction. */

    /* generation ET liaison INCHANGEES depuis l'amorce ci-dessus (aucun
     * source_etat_sim_cycle() entre les deux pompages) : sous le code casse,
     * habillage_pomper() sautait entierement le bloc de propagation et
     * ECRAN_JOUET_CALME.mettre_a_jour ne tournait jamais -- RED reellement
     * observe en ecrivant ce test (g_trace_ecran_calme.maj restait a 0 ici). */
    habillage_pomper();
    VERIFIER(g_trace_ecran_calme.maj >= 1);

    /* Pas de nouveau pompage entre-temps : la garde de redessin (generation
     * ET liaison toujours inchangees) ne doit pas re-notifier une SECONDE
     * fois pour la MEME cause -- seul le changement de sommet de pile doit
     * forcer une propagation, pas chaque pompage qui suit indefiniment. */
    habillage_pomper();
    VERIFIER(g_trace_ecran_calme.maj == 1);
}

void suite_commandes(void)
{
    /* Garde d'ordonnancement (revue finale jalon 2b) : section_echec_asynchrone()
     * ci-dessous reutilise l'habillage deja construit par
     * suite_ecran_configuration() (singleton process-wide, voir le
     * commentaire de tete de ce fichier) -- sans elle, une inversion
     * accidentelle de l'ordre d'enregistrement dans tests/main.c plantait en
     * SEGV brut, quelque part au fond de LVGL, sans le moindre compte de
     * verifications affiche. habillage_est_construit() transforme ce SEGV
     * muet en une erreur nommee des l'entree de cette suite. */
    if (!habillage_est_construit()) {
        printf("ERREUR: suite_commandes() exige que suite_ecran_configuration() ait deja construit "
               "l'habillage (singleton process-wide) -- verifier l'ordre d'enregistrement dans "
               "tests/main.c.\n");
        exit(1);
    }

    section_moonraker_chemin_commande();
    section_backend_factice_commande();
    section_ecran_accueil_boutons();
    section_echec_asynchrone();
    section_echec_urgence_priorite();
    section_file_pleine();
    section_ecran_pousse_pendant_periode_calme();
}
