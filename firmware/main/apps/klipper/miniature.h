/* Store dédié de LA miniature gcode en cours (feature "Miniatures gcode",
 * tâche B -- intégration ESP, voir
 * docs/superpowers/specs/2026-08-04-miniatures-design.md et le rapport de la
 * tâche A pour les parseurs purs consommés par ce store, moonraker_rpc.h).
 *
 * Patron verrou HORS `etat_klipper_t` -- EXACTEMENT comme klipper_fichiers.h/
 * power_devices.h (voir leurs commentaires de tête pour le POURQUOI complet :
 * `etat_klipper_t` est un POD copié PARTOUT, un champ volumineux dedans
 * multiplierait sa taille par autant de copies qu'il en existe -- c'est ce
 * défaut précis qui avait épuisé la RAM interne au point que la tâche
 * WebSocket ne pouvait plus s'allouer, voir la mémoire du projet). Une
 * miniature décodée peut peser jusqu'à MINIATURE_TAILLE_MAX_OCTETS -- il n'a
 * jamais été question de la faire transiter par `etat_klipper_t`.
 *
 * DIFFÉRENCE structurelle avec klipper_fichiers.c/power_devices.c : ceux-là
 * ne stockent que des PLAIN OLD DATA (tableaux de taille fixe, copiés sous
 * verrou). Ce store-ci possède un pointeur vers un tampon PSRAM alloué
 * dynamiquement (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` côté ESP,
 * `malloc()` côté host-test/simulateur -- voir miniature.c) : PSRAM
 * UNIQUEMENT, jamais la RAM interne, jamais une pile de tâche (priorité
 * absolue de sûreté mémoire de cette tâche).
 *
 * PROPRIÉTÉ PSRAM -- lire attentivement avant tout appel :
 *   - Le PRODUCTEUR (la tâche de fetch, miniature_fetch.c, ou moonraker_ws.c
 *     pour les transitions sans octets) dépose des résultats via
 *     miniature_poser_prete()/_echec()/_en_cours()/miniature_effacer(). Il ne
 *     libère JAMAIS lui-même l'ancien tampon PSRAM affiché : le faire
 *     libérerait potentiellement une mémoire que la tâche LVGL est encore en
 *     train de lire (ces deux tâches tournent réellement en parallèle sur cet
 *     ESP32-S3 SMP), une fuite d'ownership entre tâches, PAS un simple accès
 *     concurrent sur un verrou (voir miniature.c pour le mécanisme de
 *     transfert différé qui règle ça).
 *   - Le CONSOMMATEUR (l'écran d'impression, ecran_accueil.c, TOUJOURS sur la
 *     tâche LVGL) lit un instantané via miniature_lire() -- il reçoit alors
 *     le pointeur ACTIF du store (jamais une copie des octets, qui pourrait
 *     peser jusqu'à MINIATURE_TAILLE_MAX_OCTETS -- une copie à chaque
 *     rafraîchissement de l'écran serait elle-même un défaut de sûreté
 *     mémoire, pas une protection). Ce pointeur reste valide tant que le
 *     CONSOMMATEUR ne l'a pas remplacé par un plus récent (généralement lui
 *     survit largement, un fetch n'arrivant qu'une fois par impression) --
 *     voir le contrat détaillé de miniature_lire()/miniature_purger()
 *     ci-dessous, à respecter EXACTEMENT dans cet ordre par l'écran appelant. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "etat_klipper.h" /* KLIPPER_FICHIER_MAX */

/* Nom de fichier associé -- même borne que le reste du protocole
 * (KLIPPER_FICHIER_MAX, voir etat_klipper.h/klipper_fichiers.h). */
#define MINIATURE_NOM_MAX KLIPPER_FICHIER_MAX

/* Taille de tampon suffisante pour le chemin COMPLET d'une miniature relatif
 * à la racine "gcodes" (voir miniature_construire_chemin() dans
 * moonraker_rpc.h : `<dossier du gcode>/<relative_path>`) -- CONSTANTE
 * PARTAGÉE entre moonraker_ws.c (qui construit ce chemin et le passe à
 * miniature_fetch_lancer()) et miniature_fetch.c (qui le recopie dans son
 * contexte de tâche, voir miniature_fetch.h) : DOIT être la MÊME valeur des
 * deux côtés, sinon une recopie snprintf() tronquerait silencieusement un
 * chemin que l'autre bout avait pourtant validé comme tenant dans son propre
 * tampon -- piège qu'un simple "192 ici, 160 là" aurait introduit en
 * silence. `MINIATURE_NOM_MAX` (le fichier gcode lui-même) + marge généreuse
 * pour le `relative_path` de la miniature concaténé (typiquement
 * ".thumbs/nom-300x300.png", largement sous cette marge). */
#define MINIATURE_CHEMIN_MAX (MINIATURE_NOM_MAX + 128)

/* Largeur maximale (px) demandée à rpc_lire_miniature() pour choisir la
 * miniature à fetcher (voir moonraker_rpc.h) : modeste pour borner le décodé
 * (une miniature 300x300 décodée en ARGB8888 pèse ~360 Ko -- très au-delà de
 * ce qu'on veut garder vivant en PSRAM pour un simple thumbnail d'écran). */
#define MINIATURE_LARGEUR_MAX_PX 160

/* Taille maximale (octets) du flux PNG accepté par le fetch HTTP
 * (miniature_fetch.c) -- au-delà, l'abandon est propre (voir son
 * commentaire). 64 Kio couvre très largement un PNG de MINIATURE_LARGEUR_MAX_PX
 * de côté avec de la marge de compression défavorable. */
#define MINIATURE_TAILLE_MAX_OCTETS (64u * 1024u)

typedef enum {
    MINIATURE_ABSENTE = 0, /* aucune impression active connue avec miniature, ou jamais tentée */
    MINIATURE_EN_COURS,    /* fetch (metadata WS ou HTTP) en cours pour `fichier` */
    MINIATURE_PRETE,       /* `donnees` porte un PNG valide pour `fichier` */
    MINIATURE_ECHEC,       /* tentative pour `fichier` retombée en échec (pas de thumbnail,
                            * HTTP en échec, PNG invalide/trop gros...) -- ne sera pas
                            * retentée pour CE fichier (voir moonraker_ws.c, qui ne
                            * redemande que sur un CHANGEMENT de fichier). */
} miniature_etat_t;

/* Instantané rendu par miniature_lire() -- voir son commentaire pour le
 * contrat de propriété/durée de vie de `donnees`. */
typedef struct {
    miniature_etat_t etat;
    char             fichier[MINIATURE_NOM_MAX]; /* fichier gcode associé, "" si ABSENTE */
    const uint8_t   *donnees;   /* octets PNG en PSRAM, NULL si etat != PRETE */
    size_t           taille;    /* longueur de `donnees`, 0 si NULL */
    int32_t          largeur;   /* dimensions déclarées par Moonraker (metadata), 0 si NULL */
    int32_t          hauteur;
    uint32_t         generation; /* incrémentée à CHAQUE dépôt (poser_..., effacer) -- permet
                                  * à l'appelant de détecter un changement sans comparer le
                                  * contenu octet par octet. */
} miniature_instantane_t;

/* Marque le début d'un fetch pour `fichier` (metadata WS sur le point d'être
 * demandée) -- appelé par moonraker_ws.c dès qu'une impression active révèle
 * un `print_stats.filename` nouveau. Retire IMMÉDIATEMENT toute miniature
 * PRÊTE affichée jusqu'ici (spec : « UN seul thumbnail vivant ; libérer
 * l'ancien AVANT le nouveau ») -- ne libère jamais la PSRAM elle-même ici
 * (voir le commentaire de tête sur le transfert différé), seulement l'état
 * visible passe à EN_COURS. `fichier` NULL/vide est traité comme "" (état
 * EN_COURS quand même posé, sans nom -- ne devrait jamais arriver en
 * pratique, l'appelant garantit un nom non vide). */
void miniature_poser_en_cours(const char *fichier);

/* Dépose un résultat PRÊT pour `fichier` : prend possession de `donnees`
 * (DOIT avoir été alloué en PSRAM par l'appelant -- `heap_caps_malloc(...,
 * MALLOC_CAP_SPIRAM)` côté ESP, voir miniature_fetch.c -- jamais la RAM
 * interne, jamais une pile). Si `fichier` ne correspond PLUS au fichier
 * actuellement suivi par le store (une impression plus récente a déjà
 * redéclenché miniature_poser_en_cours() pour un AUTRE fichier pendant que ce
 * fetch était en vol), le dépôt est REJETÉ : `donnees` est libéré
 * IMMÉDIATEMENT par cet appel (sur la tâche appelante -- sûr, ce tampon n'a
 * jamais été exposé à quiconque d'autre, voir miniature_lire()) et le store
 * n'est pas modifié plus avant. Sinon, remplace l'état par PRÊT et transfère
 * l'ancien tampon actif (s'il y en avait un) vers la file d'attente de
 * libération différée -- voir miniature_purger(), à charge du consommateur.
 * `fichier` NULL/vide, ou `donnees` NULL avec `taille` non nulle (et
 * inversement), sont rejetés sans effet (défensif). */
void miniature_poser_prete(const char *fichier, uint8_t *donnees, size_t taille,
                           int32_t largeur, int32_t hauteur);

/* Dépose un échec pour `fichier` -- même garde de pertinence que
 * miniature_poser_prete() (ignoré si `fichier` ne correspond plus au fichier
 * suivi). Retire toute miniature affichée jusqu'ici (même transfert différé
 * que ci-dessus). */
void miniature_poser_echec(const char *fichier);

/* Aucune impression active/fichier connu (fin d'impression, ou fichier vide) :
 * repasse à ABSENTE, retire le fichier suivi, retire toute miniature affichée
 * (même transfert différé). Inconditionnel (pas de garde de pertinence,
 * contrairement aux deux fonctions ci-dessus) -- c'est TOUJOURS légitime de
 * tout effacer quand plus aucune impression n'est active. */
void miniature_effacer(void);

/* Copie l'instantané courant dans `*dest` (métadonnées + pointeur ACTIF,
 * jamais les octets eux-mêmes -- voir le commentaire de tête). `dest` NULL =
 * no-op. À appeler UNIQUEMENT depuis la tâche LVGL (c'est elle qui possède la
 * durée de vie du pointeur rendu, voir miniature_purger() ci-dessous) : le
 * pointeur rendu dans `dest->donnees` reste valide tant qu'aucun appel
 * ultérieur à miniature_lire() ne rend une `generation` différente -- une
 * fois qu'un appelant a agi sur une NOUVELLE génération (typiquement
 * `lv_image_set_src()` avec le nouveau pointeur), il DOIT appeler
 * miniature_purger() pour permettre au store de libérer ce qu'un dépôt
 * producteur a entre-temps retiré (voir son commentaire pour l'ordre exact à
 * respecter).
 *
 * Effet de bord VOULU : cet appel REVENDIQUE le pointeur actif rendu
 * (`g_affiche = donnees`, voir miniature.c) -- à partir d'ici et jusqu'à ce
 * que le consommateur publie un autre pointeur (via miniature_purger() ou un
 * miniature_lire() ultérieur), aucune voie de libération ne touchera ce
 * tampon. C'est ce qui rend sûr de le poser paresseusement sur le widget même
 * si un dépôt producteur survient dans l'intervalle. */
void miniature_lire(miniature_instantane_t *dest);

/* Libère le tampon PSRAM qu'un dépôt producteur (poser_prete/poser_echec/
 * poser_en_cours/effacer) a retiré depuis le dernier appel -- à appeler
 * SYSTÉMATIQUEMENT par le consommateur (ecran_accueil.c) en FIN de chaque
 * cycle de mise à jour, APRÈS avoir ré-orienté le widget LVGL (ou masqué
 * l'image si l'état n'est pas PRÊT).
 *
 * `encore_affiche` DOIT être le pointeur RÉELLEMENT référencé par le widget à
 * cet instant : `ctx->miniature_dsc.data` si l'image est visible, NULL si
 * l'écran vient de la masquer. `lv_image_set_src()` étant PARESSEUX (il ne
 * décode `data` qu'au dessin, plus tard, sur la tâche LVGL), le store REFUSE
 * de libérer tout tampon égal à `encore_affiche` -- sans quoi un dépôt
 * producteur glissé entre miniature_lire() et cet appel ferait libérer sous
 * les pieds du widget le tampon qu'il va décoder (use-after-free de la revue).
 * Le tampon retiré depuis le dernier cycle n'est donc libéré ici QUE s'il ne
 * vaut plus ni ce que le consommateur détenait (revendiqué par miniature_lire)
 * ni `encore_affiche` -- voir le mécanisme `g_affiche` dans miniature.c. Sans
 * effet s'il n'y a rien à libérer (cas le plus fréquent, appel bon marché : un
 * verrou court puis rien). */
void miniature_purger(const uint8_t *encore_affiche);
