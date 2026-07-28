/* Implémentation : voir habillage.h pour le contrat. */
#include "habillage.h"

#include <stdio.h>
#include <string.h>

#include "backend.h"
#include "etat_klipper.h"
#include "journal.h"
#include "navigation.h"
#include "plateforme.h"
#include "source_etat.h"

static const char *TAG = "habillage";

/* Mise en page imposée par le brief (l'appareil fait 800x480). */
#define LARGEUR_ECRAN    800
#define HAUTEUR_ECRAN    480
#define BARRE_HAUTEUR     44
#define BANDEAU_HAUTEUR   60
#define BANDEAU_DUREE_MS 4000

#define COULEUR_FOND_BARRE     0x1B2430
#define COULEUR_TEXTE_PRINCIPAL 0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_BOUTON_RETOUR   0x2A3644
#define COULEUR_WIFI_ACTIF      0xE8EDF2
#define COULEUR_WIFI_INACTIF    0x3A4656
#define COULEUR_BANDEAU_INFO    0x1B2430
#define COULEUR_BANDEAU_ERREUR  0xB3352C

/* --- État module : une seule barre d'état/un seul bandeau existent jamais
 * (voir le commentaire de habillage_construire() dans habillage.h). Des
 * variables statiques de fichier plutôt qu'un contexte passé en paramètre :
 * les trois fonctions publiques (construire/pomper/notifier) n'en reçoivent
 * pas, par contrat imposé par le brief. --- */
static lv_obj_t *g_barre;
static lv_obj_t *g_bouton_retour;
static lv_obj_t *g_titre;
static lv_obj_t *g_pastille_liaison;
static lv_obj_t *g_texte_liaison;
static lv_obj_t *g_wifi_barres[4];
static lv_obj_t *g_batterie;
static lv_obj_t *g_heure;
static lv_obj_t *g_contenu;
static lv_obj_t *g_bandeau;
static lv_obj_t *g_bandeau_texte;
static lv_timer_t *g_bandeau_timer;

/* Dernier état transmis à navigation_mettre_a_jour(), pour la décision de
 * propagation de habillage_pomper() (voir son commentaire dans habillage.h).
 * uint32_t boucle_generation()/etat_store_generation() valent 0 avant tout
 * premier changement réel (voir core/etat_store.h, core/boucle.h) : partir
 * de 0 ici suffit donc à déclencher la première mise à jour dès qu'un cycle
 * a réellement réussi, sans variable "premier appel" séparée. */
static uint32_t       g_derniere_generation;
static liaison_etat_t g_derniere_liaison = LIAISON_CONNEXION;

/* Tampon de lecture de l'état applicatif, relu à chaque habillage_pomper().
 * Concret (etat_klipper_t), pas void* : ui_etat_instantane() (voir
 * source_etat.h) exige un `dest`/`taille` correspondant EXACTEMENT à la
 * taille réelle de l'état du backend en cours pour rendre quoi que ce soit,
 * y compris generation/liaison — reprise du même motif que gestion_state()
 * dans firmware/main/web.c, seul autre consommateur de ce même triplet
 * (state + generation + liaison sous une seule lecture). */
static etat_klipper_t g_etat;

const char *habillage_texte_liaison(liaison_etat_t etat)
{
    switch (etat) {
    case LIAISON_CONNEXION:  return "connecting";
    case LIAISON_EN_LIGNE:   return "online";
    case LIAISON_DEGRADEE:   return "unstable";
    case LIAISON_HORS_LIGNE: return "offline";
    }
    /* Valeur hors énumération (un flux corrompu, un futur ajout non traité
     * ici) : ne jamais rendre NULL, lv_label_set_text(NULL) déréférence et
     * fait tomber l'interface entière pour une seule pastille de statut. */
    return "unknown";
}

uint32_t habillage_couleur_liaison(liaison_etat_t etat)
{
    switch (etat) {
    case LIAISON_EN_LIGNE:   return 0x2ECC71; /* vert */
    case LIAISON_DEGRADEE:   return 0xF1C40F; /* ambre */
    case LIAISON_HORS_LIGNE: return 0xE74C3C; /* rouge */
    case LIAISON_CONNEXION:  return 0x7F8C8D; /* gris */
    }
    return 0x7F8C8D;
}

bool habillage_donnees_perimees(liaison_etat_t etat)
{
    return etat != LIAISON_EN_LIGNE;
}

/* Tâche 9 : traduit une action interne (BACKEND_ACTION_PAUSE/REPRENDRE/
 * ANNULER/URGENCE, voir core/backend.h -- ce sont les seules valeurs que
 * ui_commande_echec() peut jamais rendre, puisqu'elles sont aussi les seules
 * que ecran_accueil.c empile via ui_commander()) en un mot anglais présentable
 * dans le bandeau de notification (§5.3 : texte d'interface en anglais,
 * jamais un identifiant interne francophone brut comme "reprendre"). Second
 * site de couplage à une application Klipper précise dans ce fichier par
 * ailleurs générique (voir le commentaire de tête de habillage.h) -- au même
 * titre que l'unique site déjà documenté là (ui_etat_instantane() /
 * etat_klipper_t) : un fork adapte ce fichier-ci, pas ecran.h/navigation.c.
 * Défensif sur une action non reconnue (rendue telle quelle) plutôt que de
 * planter ou d'inventer un texte qui masquerait un futur cas réel -- ne
 * devrait jamais arriver en pratique. */
static const char *libelle_commande(const char *action)
{
    if (strcmp(action, BACKEND_ACTION_PAUSE) == 0)     return "pause";
    if (strcmp(action, BACKEND_ACTION_REPRENDRE) == 0) return "resume";
    if (strcmp(action, BACKEND_ACTION_ANNULER) == 0)   return "cancel";
    if (strcmp(action, BACKEND_ACTION_URGENCE) == 0)   return "emergency stop";
    return action;
}

static void bouton_retour_cb(lv_event_t *e)
{
    (void)e;
    navigation_depiler();
}

static void construire_barre(lv_obj_t *parent)
{
    g_barre = lv_obj_create(parent);
    lv_obj_remove_style_all(g_barre);
    lv_obj_set_size(g_barre, LARGEUR_ECRAN, BARRE_HAUTEUR);
    lv_obj_set_pos(g_barre, 0, 0);
    lv_obj_set_style_bg_color(g_barre, lv_color_hex(COULEUR_FOND_BARRE), 0);
    lv_obj_set_style_bg_opa(g_barre, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(g_barre, 12, 0);
    lv_obj_set_style_pad_right(g_barre, 12, 0);
    lv_obj_clear_flag(g_barre, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(g_barre, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_barre, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER);

    /* Groupe gauche : bouton retour (masqué par défaut, voir
     * rafraichir_barre()) puis titre. */
    lv_obj_t *gauche = lv_obj_create(g_barre);
    lv_obj_remove_style_all(gauche);
    lv_obj_set_size(gauche, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(gauche, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(gauche, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gauche, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(gauche, 10, 0);

    g_bouton_retour = lv_button_create(gauche);
    lv_obj_set_size(g_bouton_retour, 32, 32);
    lv_obj_set_style_radius(g_bouton_retour, 6, 0);
    lv_obj_set_style_bg_color(g_bouton_retour, lv_color_hex(COULEUR_BOUTON_RETOUR), 0);
    lv_obj_set_style_border_width(g_bouton_retour, 0, 0);
    lv_obj_set_style_shadow_width(g_bouton_retour, 0, 0);
    lv_obj_add_flag(g_bouton_retour, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_bouton_retour, bouton_retour_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fleche = lv_label_create(g_bouton_retour);
    lv_obj_set_style_text_color(fleche, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(fleche, "<");
    lv_obj_center(fleche);

    g_titre = lv_label_create(gauche);
    lv_obj_set_style_text_font(g_titre, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_titre, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(g_titre, "");

    /* Groupe droit, dans l'ordre imposé : pastille + texte de liaison,
     * barres wifi, batterie, heure. */
    lv_obj_t *droite = lv_obj_create(g_barre);
    lv_obj_remove_style_all(droite);
    lv_obj_set_size(droite, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(droite, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(droite, LV_FLEX_FLOW_ROW);
    /* START, pas END : `droite` est déjà cadrée à droite de la barre par le
     * SPACE_BETWEEN de `g_barre` ci-dessus, elle n'a pas besoin de
     * réaligner en plus son propre contenu. LVGL combine mal END (ou
     * CENTER) avec un conteneur LV_SIZE_CONTENT sur l'axe principal : la
     * taille de contenu se cale sur le dernier enfant seul, et les
     * précédents se retrouvent positionnés à des abscisses négatives, hors
     * cadre — constaté directement (voir task-4-report.md, la barre
     * n'affichait que l'heure, tout le reste à x < 0). START élimine le
     * problème puisqu'il n'y a alors aucun espace superflu à répartir. */
    lv_obj_set_flex_align(droite, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(droite, 14, 0);

    g_pastille_liaison = lv_obj_create(droite);
    lv_obj_remove_style_all(g_pastille_liaison);
    lv_obj_set_size(g_pastille_liaison, 10, 10);
    lv_obj_set_style_radius(g_pastille_liaison, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g_pastille_liaison, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_pastille_liaison, LV_OBJ_FLAG_SCROLLABLE);

    g_texte_liaison = lv_label_create(droite);
    lv_obj_set_style_text_color(g_texte_liaison, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(g_texte_liaison, "");

    /* Barres wifi : quatre rectangles de hauteur croissante, alignés en bas
     * comme une icône de signal usuelle, coloriés une à une jusqu'à
     * `barres` par rafraichir_barre(). */
    lv_obj_t *wifi = lv_obj_create(droite);
    lv_obj_remove_style_all(wifi);
    lv_obj_set_size(wifi, 18, 14);
    lv_obj_clear_flag(wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(wifi, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(wifi, 2, 0);
    static const lv_coord_t hauteurs[4] = {4, 7, 10, 13};
    for (int i = 0; i < 4; i++) {
        g_wifi_barres[i] = lv_obj_create(wifi);
        lv_obj_remove_style_all(g_wifi_barres[i]);
        lv_obj_set_size(g_wifi_barres[i], 3, hauteurs[i]);
        lv_obj_set_style_bg_opa(g_wifi_barres[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(g_wifi_barres[i], lv_color_hex(COULEUR_WIFI_INACTIF), 0);
        lv_obj_set_style_radius(g_wifi_barres[i], 1, 0);
        lv_obj_clear_flag(g_wifi_barres[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    g_batterie = lv_label_create(droite);
    lv_obj_set_style_text_color(g_batterie, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(g_batterie, "");

    g_heure = lv_label_create(droite);
    lv_obj_set_style_text_color(g_heure, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(g_heure, "");
}

static void construire_bandeau(lv_obj_t *parent)
{
    g_bandeau = lv_obj_create(parent);
    lv_obj_remove_style_all(g_bandeau);
    lv_obj_set_size(g_bandeau, LARGEUR_ECRAN, BANDEAU_HAUTEUR);
    lv_obj_set_pos(g_bandeau, 0, HAUTEUR_ECRAN - BANDEAU_HAUTEUR);
    lv_obj_set_style_bg_opa(g_bandeau, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_bandeau, lv_color_hex(COULEUR_BANDEAU_INFO), 0);
    lv_obj_clear_flag(g_bandeau, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_bandeau, LV_OBJ_FLAG_HIDDEN);

    g_bandeau_texte = lv_label_create(g_bandeau);
    lv_obj_set_style_text_font(g_bandeau_texte, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_bandeau_texte, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(g_bandeau_texte, "");
    lv_obj_center(g_bandeau_texte);
}

void habillage_construire(lv_obj_t *ecran_racine)
{
    if (g_barre != NULL) {
        JOURNAL_ALERTE(TAG, "habillage_construire appele une seconde fois ; ignore");
        return;
    }
    if (ecran_racine == NULL) {
        JOURNAL_ERREUR(TAG, "habillage_construire : ecran_racine NULL");
        return;
    }

    construire_barre(ecran_racine);

    /* Zone de contenu : le reste (800x436), remis à navigation_init() sans
     * aucun style hérité — c'est à chaque écran de poser son propre fond
     * (même politique que navigation_empiler() applique déjà à chaque
     * conteneur d'écran, voir navigation.c). */
    g_contenu = lv_obj_create(ecran_racine);
    lv_obj_remove_style_all(g_contenu);
    lv_obj_set_size(g_contenu, LARGEUR_ECRAN, HAUTEUR_ECRAN - BARRE_HAUTEUR);
    lv_obj_set_pos(g_contenu, 0, BARRE_HAUTEUR);
    lv_obj_clear_flag(g_contenu, LV_OBJ_FLAG_SCROLLABLE);
    navigation_init(g_contenu);

    /* Bandeau créé en dernier : dernier enfant de `ecran_racine`, donc
     * toujours au-dessus de la barre et de la zone de contenu en z-order
     * LVGL, quoi que la navigation empile ou dépile ensuite à l'intérieur
     * de g_contenu (un sous-arbre séparé, jamais réordonné par rapport à
     * ses frères). */
    construire_bandeau(ecran_racine);

    g_derniere_generation = 0;
    g_derniere_liaison = LIAISON_CONNEXION;
}

static void rafraichir_barre(liaison_etat_t liaison)
{
    const char *titre = navigation_titre_courant();
    lv_label_set_text(g_titre, titre != NULL ? titre : "");

    if (navigation_profondeur() > 1) {
        lv_obj_clear_flag(g_bouton_retour, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_bouton_retour, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_style_bg_color(g_pastille_liaison, lv_color_hex(habillage_couleur_liaison(liaison)), 0);
    lv_label_set_text(g_texte_liaison, habillage_texte_liaison(liaison));

    plateforme_wifi_t wifi;
    plateforme_wifi(&wifi);
    for (int i = 0; i < 4; i++) {
        bool actif = wifi.associe && i < wifi.barres;
        lv_obj_set_style_bg_color(g_wifi_barres[i],
                                   lv_color_hex(actif ? COULEUR_WIFI_ACTIF : COULEUR_WIFI_INACTIF), 0);
    }

    plateforme_batterie_t batterie;
    plateforme_batterie(&batterie);
    if (batterie.valide) {
        char tampon[8];
        snprintf(tampon, sizeof(tampon), "%s%u%%", batterie.en_charge ? "+" : "",
                 (unsigned)batterie.pourcentage);
        lv_label_set_text(g_batterie, tampon);
        lv_obj_clear_flag(g_batterie, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_batterie, LV_OBJ_FLAG_HIDDEN);
    }

    plateforme_heure_t heure;
    plateforme_heure(&heure);
    if (heure.valide) {
        char tampon[8];
        snprintf(tampon, sizeof(tampon), "%02u:%02u", (unsigned)heure.heures, (unsigned)heure.minutes);
        lv_label_set_text(g_heure, tampon);
        lv_obj_clear_flag(g_heure, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_heure, LV_OBJ_FLAG_HIDDEN);
    }
}

void habillage_pomper(void)
{
    if (g_barre == NULL) {
        /* habillage_construire() n'a pas (encore) été appelé : rien à
         * rafraîchir, et ui_etat_instantane() n'aurait de toute façon rien
         * à transmettre à une navigation qui n'existe pas. */
        return;
    }

    /* Tâche 9 : remonte l'échec ASYNCHRONE d'une commande (exécutée par la
     * boucle APRÈS que ui_commander() a déjà rendu la main à son appelant
     * d'origine, voir le commentaire de ui_commande_echec() dans
     * source_etat.h) avant toute autre chose ici -- indépendant de la
     * génération de l'état, qui ne change pas forcément quand une commande
     * échoue (une pause refusée laisse l'imprimante dans le même état). */
    char action_echouee[UI_COMMANDE_ACTION_MAX];
    if (ui_commande_echec(action_echouee, sizeof(action_echouee))) {
        char texte[64];
        snprintf(texte, sizeof(texte), "Command failed: %s", libelle_commande(action_echouee));
        habillage_notifier(texte, true);
    }

    uint32_t generation = 0;
    liaison_etat_t liaison = LIAISON_CONNEXION;
    bool disponible = ui_etat_instantane(&g_etat, sizeof(g_etat), &generation, &liaison);

    /* Rafraîchie à chaque appel, que la boucle ait démarré ou non : une
     * boucle non démarrée doit se lire "connecting", pas rester figée sur
     * un premier rendu jamais mis à jour. */
    rafraichir_barre(liaison);

    if (!disponible) {
        return;
    }

    if (generation != g_derniere_generation || liaison != g_derniere_liaison) {
        /* Calculé une seule fois ici, à partir de la SEULE liaison (jamais
         * du contenu de g_etat) : c'est le seul appel réel de
         * habillage_donnees_perimees() de tout le module, et le seul point
         * de passage entre la règle 5.3 et un écran (voir ecran.h,
         * mettre_a_jour). La condition de propagation ci-dessus couvre déjà
         * ce cas sans rien ajouter : un changement de liaison a changer
         * `liaison != g_derniere_liaison` la fait de toute façon entrer
         * dans ce bloc. */
        bool perimees = habillage_donnees_perimees(liaison);
        navigation_mettre_a_jour(&g_etat, perimees);
        g_derniere_generation = generation;
        g_derniere_liaison = liaison;
    }
}

static void bandeau_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_bandeau != NULL) {
        lv_obj_add_flag(g_bandeau, LV_OBJ_FLAG_HIDDEN);
    }
    g_bandeau_timer = NULL;
}

void habillage_notifier(const char *texte, bool erreur)
{
    if (g_bandeau == NULL || g_bandeau_texte == NULL) {
        JOURNAL_ALERTE(TAG, "habillage_notifier appele avant habillage_construire ; ignore");
        return;
    }

    lv_label_set_text(g_bandeau_texte, texte != NULL ? texte : "");
    lv_obj_set_style_bg_color(
        g_bandeau, lv_color_hex(erreur ? COULEUR_BANDEAU_ERREUR : COULEUR_BANDEAU_INFO), 0);
    lv_obj_clear_flag(g_bandeau, LV_OBJ_FLAG_HIDDEN);

    /* Une notification déjà affichée est remplacée, jamais empilée : le
     * minuteur en cours (s'il en reste un — un timer à répétition unique se
     * supprime lui-même après avoir tourné, voir bandeau_timer_cb()) est
     * supprimé et un nouveau, plein, prend sa place. */
    if (g_bandeau_timer != NULL) {
        lv_timer_delete(g_bandeau_timer);
        g_bandeau_timer = NULL;
    }
    g_bandeau_timer = lv_timer_create(bandeau_timer_cb, BANDEAU_DUREE_MS, NULL);
    lv_timer_set_repeat_count(g_bandeau_timer, 1);
}
