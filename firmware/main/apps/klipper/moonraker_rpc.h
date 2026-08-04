/* Protocole JSON-RPC 2.0 de Moonraker (transport WebSocket) en fonctions
 * pures : construction de requêtes, classification et interprétation des
 * messages entrants, fusion PARTIELLE dans l'état v2. Aucune de ces
 * fonctions ne touche au réseau ni n'alloue de mémoire persistante — c'est
 * ce qui les rend testables entièrement sur PC (voir host-test/), au même
 * titre que moonraker_parse.c pour le sondage HTTP existant.
 *
 * Décision de « poison » (documentée ici une fois pour toutes, appliquée
 * partout dans ce fichier) : un champ non fini (NaN/Inf) ou d'un type
 * inattendu à l'intérieur d'un objet de statut ne poisonne QUE ce champ —
 * il est laissé inchangé et le reste du message continue d'être appliqué.
 * Seules des anomalies d'ENVELOPPE (JSON illisible, imbrication hostile,
 * `params` absent ou n'étant pas un tableau, `params[0]` n'étant pas un
 * objet) invalident le message ENTIER : dans ce cas `rpc_fusionner_status`
 * rend false et ne touche RIEN à `*etat`. Ce choix suit le même principe
 * que moonraker_parse.c (une valeur individuelle absente ou invalide garde
 * sa valeur par défaut/existante, elle ne fait jamais échouer toute
 * l'analyse) et le contrat central de ce module : Moonraker ne pousse que
 * ce qui a changé, un champ mal formé dans un coin du message ne doit pas
 * faire perdre les autres champs, légitimes, du même message.
 *
 * Fix round 1 (revue) : une valeur FINIE mais hors de la plage du type
 * entier cible (ex. `M220 S100000` -> speed_factor 1000.0 -> 100000 %, hors
 * plage d'un uint16_t) n'est PAS un poison au sens ci-dessus — c'est une
 * commande réelle et démesurée, exactement comme `moonraker_parse.c`
 * plafonne `temps_restant_s` à `KLIPPER_TEMPS_RESTANT_MAX_S` plutôt que de
 * le rejeter. Ces conversions sont donc CLAMPÉES avant la conversion en
 * entier (jamais tronquées après coup : C11 6.3.1.4 fait de la conversion
 * d'un flottant hors plage ou non fini vers un entier un comportement
 * indéfini), alors qu'une valeur non finie reste soumise à la règle
 * ci-dessus (champ laissé inchangé). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "etat_klipper.h"
#include "klipper_fichiers.h"
#include "power_devices.h"

/* Construit une requête JSON-RPC 2.0 : `{"jsonrpc":"2.0","method":"...",
 * "params":<params_json>,"id":N}`. Si `params_json` est NULL, la clé
 * "params" est omise entièrement (pas de "params":null). `id` est fourni
 * par l'appelant (le corrélateur de la tâche 5 les génère). Rend false SANS
 * qu'aucune garantie ne soit donnée sur le contenu de `sortie` si le tampon
 * est trop court (jamais de troncature silencieuse rendue à l'appelant
 * comme un succès — leçon du 2b) ; l'appelant ne doit émettre `sortie`
 * qu'après un retour true. `params_json`, s'il est fourni, doit déjà être
 * du JSON valide (objet/tableau/scalaire) : cette fonction ne le valide
 * pas, elle le recopie tel quel. */
bool rpc_construire_requete(char *sortie, size_t taille, uint32_t id,
                            const char *methode, const char *params_json);

/* Taille de tampon suffisante pour tout appel de rpc_construire_abonnement()
 * ci-dessous, id compris (besoin mesuré : 349 octets avec un id à 10
 * chiffres, le maximum d'un uint32_t — voir le test qui construit
 * l'abonnement avec id = UINT32_MAX dans ce tampon exact). Marge incluse
 * pour un futur objet ajouté à la liste sans avoir à recalculer cette
 * constante à la main à chaque fois. */
#define RPC_ABONNEMENT_TAILLE_MIN 384

/* Requête `printer.objects.subscribe` (nom de méthode JSON-RPC Moonraker :
 * des POINTS, jamais de '/' — Moonraker dérive ses méthodes par
 * `".".join(...)`, il n'existe aucun alias HTTP-like ; confondre les deux
 * formes rend -32601 "Method not found" côté Moonraker, aucune notification
 * n'arrive jamais, et le client retombe silencieusement en repli HTTP sans
 * le moindre signal d'erreur — le pire symptôme à diagnostiquer) avec les
 * objets dont l'état v2 a besoin (toolhead, gcode_move, extruder..
 * extruder7, heater_bed, fan, print_stats, virtual_sdcard, webhooks,
 * firmware_retraction).
 * speed_factor/extrude_factor sont portés par gcode_move, déjà dans la
 * liste — pas d'entrée séparée. Même contrat de tampon que
 * rpc_construire_requete() ci-dessus ; voir RPC_ABONNEMENT_TAILLE_MIN pour
 * un tampon garanti suffisant. */
bool rpc_construire_abonnement(char *sortie, size_t taille, uint32_t id);

typedef enum {
    RPC_MSG_REPONSE,          /* result ou error, avec id numérique */
    RPC_MSG_STATUS_UPDATE,    /* notify_status_update */
    RPC_MSG_KLIPPY_READY,     /* notify_klippy_ready */
    RPC_MSG_KLIPPY_DECONNECTE,/* notify_klippy_disconnected OU
                                * notify_klippy_shutdown (MCU shutdown,
                                * thermal runaway...) : dans les deux cas
                                * Klippy n'est plus utilisable et les
                                * notify_status_update cessent d'arriver —
                                * même signal côté appelant, qui doit cesser
                                * de faire confiance à l'état poussé. */
    RPC_MSG_AUTRE,            /* notification reconnue comme telle mais ignorée.
                               * ATTENTION (fixtures tâche 4, vérifié contre un
                               * VRAI Klipper) : notify_gcode_response tombe ici,
                               * or c'est LE canal par lequel Klipper signale
                               * l'échec d'une macro inconnue — la réponse RPC
                               * corrélée de printer.gcode.script, elle, rend
                               * {"result":"ok"} dans les deux cas (voir
                               * rpc_lire_reponse ci-dessous et
                               * docs/dev/klipper-simule.md). L'écran macros
                               * (tâche 6) ne peut PAS détecter un échec par la
                               * seule réponse corrélée. */
    RPC_MSG_INVALIDE,         /* JSON illisible, ou ni réponse ni notification */
} rpc_message_type_t;

/* Classifie un message entrant sans le consommer (ne modifie aucun état).
 * `id_sortie` (si non NULL) reçoit l'id UNIQUEMENT quand le résultat est
 * RPC_MSG_REPONSE (mis à 0 dans tous les autres cas, y compris invalide).
 * Une réponse sans id numérique (`result`/`error` présent mais `id` absent,
 * non numérique, ou dont la valeur n'est pas finie une fois convertie —
 * ex. 1e400 devient +infini) est classée RPC_MSG_INVALIDE : un id est la
 * seule façon de corréler la réponse à la requête qui l'a provoquée, une
 * réponse qu'on ne peut pas corréler n'est pas exploitable. Un id
 * numérique mais hors de la plage d'un uint32_t (négatif, ou >= 2^32) est
 * CLAMPÉ à [0, UINT32_MAX] plutôt que rejeté (même logique que les
 * conversions de rpc_fusionner_status, voir tête de fichier) : la
 * conversion ne produit alors jamais de comportement indéfini, au prix
 * d'une corrélation qui échouera simplement à trouver la requête en
 * attente côté appelant — pas pire qu'un id inventé de toutes pièces. */
rpc_message_type_t rpc_classifier(const char *json, size_t longueur, uint32_t *id_sortie);

/* Fusionne un message `notify_status_update` DANS un état existant (mise à
 * jour partielle : Moonraker ne pousse que ce qui a changé, donc tout champ
 * absent du message reste à sa valeur courante dans `*etat`). Rend false et
 * ne touche RIEN à `*etat` si le JSON est illisible, ou si l'enveloppe est
 * hostile (`params` absent/pas un tableau, `params[0]` pas un objet) — voir
 * la décision de poison en tête de fichier pour la différence avec les
 * anomalies internes à un champ, qui elles n'invalident que ce champ.
 *
 * Objets de statut reconnus : `toolhead` (position[0..2], homed_axes en
 * masque bit0=X bit1=Y bit2=Z, extruder = nom de l'outil actif),
 * `gcode_move` (speed_factor/extrude_factor en fraction -> vitesse_pct/
 * flux_pct en % CLAMPÉS à [0, 65535], homing_origin[2] en mm ->
 * babystep_z_um arrondi au plus proche et CLAMPÉ à la plage d'un int32_t,
 * absolute_coordinates -> deplacement_absolu), `extruder`..`extruder7`
 * (index tiré du NOM de l'objet ; hors bornes -> objet ignoré, reste du
 * message appliqué normalement), `heater_bed`, `fan` (mappé sur
 * ventilateurs[0], seul ventilateur nommément connu du protocole),
 * `print_stats` (state/filename, plus impression_en_cours/en_pause et
 * temps_restant_s recalculés — même formule que moonraker_parse_status(),
 * voir moonraker_estimer_temps_restant_s() dans moonraker_parse.h : une
 * seule source de vérité pour l'estimation, que l'état vienne du sondage
 * HTTP ou de la fusion WebSocket), `virtual_sdcard` (progress ->
 * progression). `nb_extrudeurs` est recalculé après fusion comme le compte
 * de `presente` sur les 8 emplacements, pas seulement ceux touchés par ce
 * message.
 *
 * `presente`/`present` (chauffeurs et ventilateurs) ne s'activent QUE si
 * l'objet JSON porte au moins un champ reconnu (temperature/target/speed) —
 * PAS sur la simple présence de la clé. Klipper renvoie `{}` pour un objet
 * interrogé mais absent de la machine (voir rpc_fusionner_instantane
 * ci-dessous, qui partage ce même comportement) : sans cette garde, une
 * machine mono-extrudeur verrait `nb_extrudeurs` gonfler à 8 dès le premier
 * instantané, l'abonnement demandant extruder..extruder7 sans savoir à
 * l'avance combien existent réellement sur la machine cible. */
bool rpc_fusionner_status(etat_klipper_t *etat, const char *json, size_t longueur);

/* Fusionne l'instantané initial d'une réponse à `printer.objects.subscribe`
 * (`result.status`, PAS `params[0]` — c'est la réponse corrélée à la
 * requête d'abonnement elle-même, pas une notification poussée ensuite)
 * dans un état existant. Réutilise EXACTEMENT le même moteur de fusion par
 * objet que rpc_fusionner_status() ci-dessus (même reconnaissance
 * d'objets, même garde `presente`/`present`, même recalcul de
 * `nb_extrudeurs` et de `temps_restant_s`), donc les mêmes règles de poison
 * par champ s'appliquent terme à terme.
 *
 * Sans cet appel, le premier état complet (homed_axes, speed_factor,
 * outil actif, nom de fichier en cours...) resterait aux valeurs par
 * défaut de `etat_klipper_t` jusqu'à ce que l'utilisateur déclenche par
 * hasard un notify_status_update qui les touche — l'écran mentirait par
 * omission sur tout ce qui n'a pas changé depuis la connexion.
 *
 * Rend false et ne touche RIEN à `*etat` si le JSON est illisible ou si
 * `result.status` n'est pas un objet (même contrat que
 * rpc_fusionner_status() pour les anomalies d'enveloppe). */
bool rpc_fusionner_instantane(etat_klipper_t *etat, const char *json, size_t longueur);

/* Extrait résultat/erreur d'une réponse JSON-RPC déjà corrélée par
 * l'appelant (voir rpc_classifier ci-dessus pour l'id).
 *
 * LIMITE PROTOCOLAIRE VÉRIFIÉE (fixtures tâche 4, contre un vrai Klipper —
 * host-test/fixtures/moonraker/macro-inexistante.jsonl) : pour
 * printer.gcode.script, une macro/commande INCONNUE rend quand même
 * {"result":"ok"} — indiscernable d'un succès par cette fonction. Le texte
 * d'erreur réel ("// Unknown command:...") arrive séparément, de façon
 * asynchrone, via la notification notify_gcode_response (classée
 * RPC_MSG_AUTRE aujourd'hui). Un appelant qui veut détecter l'échec d'une
 * macro doit interpréter ce canal-là ; cette fonction ne détecte que les
 * erreurs JSON-RPC (méthode inconnue, paramètres invalides, Klippy down).
 *
 * Rend false si le
 * JSON est illisible ou ne contient ni "result" ni une erreur EXPLOITABLE.
 * Sinon rend true et `*succes` indique lequel des deux prévaut : false
 * seulement si "error" est un OBJET JSON réel (pas juste une clé présente
 * — un "error":null à côté d'un "result" valide, qu'un client taquin ou un
 * bug d'un autre composant pourrait produire, ne doit jamais se lire comme
 * un échec) ; true sinon, y compris quand "result" est absent mais
 * qu'"error" n'est pas exploitable (ce dernier cas ne devrait jamais
 * arriver avec un vrai Moonraker mais ne fait pas planter l'appelant). Le
 * message Klipper de l'erreur, s'il existe, est recopié dans
 * `erreur_texte`, tronqué à `taille_erreur` SANS jamais couper au milieu
 * d'une séquence UTF-8 multi-octets — un message Klipper peut contenir des
 * caractères accentués ou "°C" ; une coupe aveugle produirait une chaîne
 * mal formée en fin de tampon. Si `*succes` est true, ou si l'erreur n'a
 * pas de champ "message" exploitable, `erreur_texte` reçoit une chaîne
 * vide. `erreur_texte`/`taille_erreur` peuvent être omis (NULL/0) si
 * l'appelant ne veut pas le texte. */
bool rpc_lire_reponse(const char *json, size_t longueur, bool *succes,
                      char *erreur_texte, size_t taille_erreur);

/* Extrait la liste des macros depuis une réponse `printer.objects.list`
 * (`result.objects`, tableau de chaînes) ou `configfile` (`result.status.
 * configfile.config`, objet dont les clés sont les noms de section) —
 * les deux partagent la même convention de nommage Moonraker/Klipper,
 * "gcode_macro NOM", donc le même filtre s'applique aux deux sans
 * distinction. Remplace ENTIÈREMENT `etat->macros`/`nb_macros`/
 * `macros_tronquees` (contrairement à rpc_fusionner_status, ce n'est pas
 * une fusion partielle : chaque appel porte l'instantané complet connu de
 * Moonraker à cet instant). Rend false et ne touche RIEN à `*etat` si le
 * JSON est illisible ou ne contient aucune des deux formes reconnues.
 *
 * Les macros `_préfixées` NE SONT PAS filtrées ici : c'est un choix
 * d'affichage, pas de protocole — l'UI (tâche 6) filtrera. Tronqué à
 * KLIPPER_MACROS_MAX avec `macros_tronquees = true` au-delà. Un nom de
 * macro d'au moins KLIPPER_MACRO_NOM_MAX caractères est IGNORÉ (pas
 * tronqué) : un nom tronqué désignerait potentiellement une AUTRE macro
 * existante et l'exécuterait par erreur — c'est le genre d'ambiguïté qui
 * ne doit jamais atteindre l'UI. Cette omission ne positionne PAS
 * `macros_tronquees` (qui documente spécifiquement le dépassement de
 * KLIPPER_MACROS_MAX, une limite de compte, pas de longueur de nom) ; une
 * macro ainsi ignorée est silencieuse au niveau protocole. */
bool rpc_lire_macros(etat_klipper_t *etat, const char *json, size_t longueur);

/* Extrait la liste des fichiers gcode depuis une réponse `server.files.list`
 * (`result`, tableau d'OBJETS -- PAS `result.objects`, tableau de chaînes,
 * contrairement à rpc_lire_macros ci-dessus) : chaque élément porte un champ
 * `.path` (chaîne, éventuellement avec des '/' pour un sous-dossier, ex.
 * "sub/b.gcode") qui devient une entrée de `dest->noms`. Remplace
 * ENTIÈREMENT `dest->noms`/`dest->nb`/`dest->tronques` (contrairement à
 * rpc_fusionner_status, ce n'est pas une fusion partielle : chaque appel porte
 * l'instantané complet connu de Moonraker à cet instant, même contrat que
 * rpc_lire_macros). Rend false et ne touche RIEN à `*dest` si le JSON est
 * illisible ou si `result` est absent ou n'est pas un tableau.
 *
 * Fonction PURE : elle écrit dans le `klipper_fichiers_t` fourni par
 * l'appelant, sans toucher à aucun global ni prendre de verrou -- c'est
 * l'appelant (moonraker_ws.c) qui déposera ensuite le résultat dans le store
 * partagé via klipper_fichiers_definir() (voir klipper_fichiers.h). C'est ce
 * qui la garde entièrement testable sur PC.
 *
 * Tronqué à KLIPPER_FICHIERS_MAX avec `tronques = true` au-delà. Un `.path`
 * d'au moins KLIPPER_FICHIER_MAX caractères est IGNORÉ (pas tronqué, même
 * leçon que rpc_lire_macros sur les noms de macro trop longs -- un chemin
 * tronqué désignerait potentiellement un AUTRE fichier existant) ; cette
 * omission ne compte pas dans `nb` et ne positionne PAS `tronques` (qui
 * documente spécifiquement le dépassement du COMPTE, pas une longueur de
 * chemin). Un élément du tableau qui n'est pas un objet, ou sans champ `.path`
 * de type chaîne, est ignoré (défensif, même esprit poison-par-champ que le
 * reste de ce fichier) -- le reste de la liste continue d'être traité
 * normalement. */
bool rpc_lire_fichiers(klipper_fichiers_t *dest, const char *json, size_t longueur);

/* ------------------------------------------------------------------------
 * Miniatures gcode (feature "Miniatures gcode", tâche A -- cœur host,
 * intégration ESP laissée à la tâche B, voir
 * docs/superpowers/specs/2026-08-04-miniatures-design.md)
 * ---------------------------------------------------------------------- */

/* Extrait, depuis une réponse `server.files.metadata` (`result.thumbnails`,
 * tableau d'objets `{width,height,size,relative_path}`), la meilleure
 * miniature disponible : la plus GRANDE largeur telle que `width <=
 * largeur_max`, ou -- si aucune ne satisfait cette borne -- la plus PETITE
 * largeur disponible (jamais d'échec juste parce que la machine ne fournit
 * que des miniatures plus grandes que `largeur_max` ; l'appelant peut
 * toujours redimensionner à l'affichage). En cas d'égalité de largeur entre
 * plusieurs candidats (même dans le tableau source), le PREMIER rencontré
 * dans l'ordre du tableau l'emporte, aussi bien pour la sélection <= max que
 * pour le repli sur la plus petite -- choix déterministe documenté ici pour
 * ne pas dépendre d'un ordre de tri caché.
 *
 * `json`/`n` portent le message JSON-RPC COMPLET (avec son enveloppe
 * `result`), pas seulement le contenu de `result` -- même convention que
 * rpc_lire_fichiers() ci-dessus (voir moonraker_ws.c : le corrélateur passe
 * toujours `json, longueur` bruts du message reçu, jamais un sous-arbre déjà
 * déballé).
 *
 * `chemin_dest` reçoit `relative_path` du candidat retenu, tronqué
 * proprement (UTF-8-safe, voir copier_texte_utf8_borne() dans
 * moonraker_rpc.c) si `chemin_n` est trop petit pour le recevoir en entier --
 * CONTRAIREMENT à rpc_lire_fichiers()/rpc_lire_macros() (où un nom trop long
 * est IGNORÉ pour ne jamais désigner par erreur une AUTRE entrée existante),
 * un `relative_path` de miniature est un simple SEGMENT d'URL construit par
 * l'appelant (voir miniature_construire_chemin() ci-dessous) : une
 * troncature ne fait qu'échouer le fetch HTTP qui suit (chemin introuvable
 * côté Moonraker), elle ne risque jamais de pointer par erreur vers un AUTRE
 * fichier existant. `*w_out`/`*h_out` reçoivent les dimensions du candidat
 * retenu (jamais celles d'un autre).
 *
 * Un élément du tableau `thumbnails` est un candidat valide seulement s'il
 * est un objet portant `width`/`height` (nombres finis STRICTEMENT positifs
 * -- 0 ou négatif est rejeté comme non exploitable) et `relative_path`
 * (chaîne non vide) ; tout élément qui ne l'est pas est IGNORÉ (poison PAR
 * ÉLÉMENT, même esprit que traiter_candidat_fichier() dans moonraker_rpc.c),
 * le reste du tableau continue d'être examiné normalement.
 *
 * Rend false SANS TOUCHER `chemin_dest`/`*w_out`/`*h_out` si `json` est
 * illisible, si `result.thumbnails` est absent ou n'est pas un tableau, si
 * le tableau est vide, ou si aucun élément du tableau n'est un candidat
 * valide (poison par élément généralisé à tout le tableau). Rend aussi false
 * SANS RIEN LIRE si `chemin_dest`/`w_out`/`h_out`/`json` est NULL ou si
 * `chemin_n`/`n` vaut 0. */
bool rpc_lire_miniature(char *chemin_dest, size_t chemin_n, int *w_out, int *h_out,
                        const char *json, size_t n, int largeur_max);

/* Recompose le chemin d'une miniature RELATIF À LA RACINE "gcodes" de
 * Moonraker (celui à mettre dans l'URL
 * `.../server/files/gcodes/<résultat de cette fonction>`, voir la spec
 * miniatures §Chaîne Moonraker) : `relative_path` (tel que rendu par
 * rpc_lire_miniature() ci-dessus) est documenté par Moonraker comme relatif
 * au DOSSIER du fichier gcode, pas à la racine "gcodes" -- il faut donc lui
 * préfixer le dossier de `gcode_chemin` (tout ce qui précède le DERNIER '/'
 * de `gcode_chemin`, s'il y en a un).
 *
 * `gcode_chemin` SANS aucun '/' (fichier à la racine "gcodes", ex.
 * "piece.gcode") : le résultat est `relative_path` tel quel, sans préfixe ni
 * '/' ajouté -- le dossier du fichier EST la racine "gcodes", rien à
 * préfixer. `gcode_chemin` avec un ou plusieurs '/' (ex. "dir/piece.gcode"
 * ou "a/b/piece.gcode") : le résultat est `<tout ce qui précède le dernier
 * '/'>/<relative_path>` (ex. "dir/relative_path", "a/b/relative_path").
 *
 * Contrat de retour À LA snprintf() : rend la longueur (hors octet nul) que
 * le résultat COMPLET aurait occupée, MÊME si `n` est trop petit pour la
 * recevoir en entier -- l'appelant compare le retour à `n` pour détecter une
 * troncature, exactement comme snprintf() lui-même (voir
 * rpc_construire_requete() plus haut pour la même convention appliquée à un
 * `bool`, ici un `size_t` puisque l'appelant peut vouloir connaître la
 * taille de tampon qui aurait suffi). `dest` peut être NULL si `n` vaut 0
 * (même règle que snprintf() : sert alors uniquement à calculer la longueur
 * nécessaire sans rien écrire). `gcode_chemin`/`relative_path` NULL sont
 * traités comme des chaînes vides (jamais de déréférencement NULL).
 *
 * Défensif -- ne déborde JAMAIS `dest` (borné par `n` via snprintf() en
 * interne) quel que soit le contenu de `relative_path`, y compris un
 * `relative_path` commençant par '/' (produit alors un double '/' dans le
 * résultat -- pas de cas particulier pour l'éliminer : cette fonction ne
 * fait que joindre deux segments de chaîne, elle ne réalise PAS le chemin
 * résultant sur un système de fichiers, elle construit un simple segment
 * d'URL que Moonraker interprète et valide de son côté) ou contenant `..`
 * (non filtré ici, même raison : c'est Moonraker, côté serveur, qui résout
 * et borne l'accès réel au système de fichiers derrière cette URL -- cette
 * fonction n'a par construction aucune vue sur le système de fichiers, une
 * tentative de filtrage ici serait une fausse garantie de sécurité). Le SEUL
 * invariant garanti ici est l'absence de débordement mémoire. */
size_t miniature_construire_chemin(char *dest, size_t n, const char *gcode_chemin,
                                   const char *relative_path);

/* ------------------------------------------------------------------------
 * Prises Moonraker (machine.device_power.*)
 * ---------------------------------------------------------------------- */

/* Extrait la liste des prises depuis une réponse à
 * `machine.device_power.devices` : `result.devices`, tableau d'objets
 * `{"device":"nom","status":"on|off",...}` (même forme d'objet que
 * `traiter_candidat_fichier` pour `server.files.list`, un champ `.path` en
 * moins et deux champs `.device`/`.status` en plus). Accepte AUSSI
 * `result` étant DIRECTEMENT le tableau `{"devices":[...]}` sans
 * l'enveloppe `result` (au cas où l'appelant recevrait un message déjà
 * partiellement déballé) -- dans les deux cas c'est la clé `"devices"`,
 * qu'elle soit sous `result` ou à la racine, qui porte le tableau.
 *
 * Remplace ENTIÈREMENT `dest->devices`/`dest->nb`/`dest->tronque` (même
 * contrat que rpc_lire_fichiers/rpc_lire_macros : instantané complet, pas
 * une fusion). Rend false et ne touche RIEN à `*dest` si le JSON est
 * illisible ou si aucune des deux formes n'est reconnue.
 *
 * `status == "on"` -> `allumee = true` ; toute autre valeur (typiquement
 * `"off"`) -> `allumee = false` -- dans les deux cas `connue = true`,
 * puisqu'un état a bien été reçu pour cette prise. Un élément du tableau qui
 * n'est pas un objet, ou dont `.device` n'est pas une chaîne, est ignoré
 * (même défense poison-par-élément que traiter_candidat_fichier) -- le
 * reste de la liste continue d'être traité normalement.
 *
 * Tronqué à POWER_DEVICES_MAX avec `tronque = true` au-delà. Un `.device`
 * d'au moins POWER_NOM_MAX caractères est IGNORÉ (pas tronqué, même leçon
 * que rpc_lire_fichiers sur les chemins trop longs -- un nom tronqué
 * désignerait potentiellement une AUTRE prise existante) ; cette omission
 * ne compte pas dans `nb` et ne positionne PAS `tronque`.
 *
 * Fonction PURE : n'importe quel appel de power_devices_definir() pour
 * déposer le résultat dans le store partagé est à la charge de
 * l'appelant (moonraker_ws.c), comme rpc_lire_fichiers/klipper_fichiers. */
bool rpc_lire_power_devices(power_devices_t *dest, const char *json, size_t longueur);

/* Extrait la liste des prises CHANGÉES depuis une notification
 * `notify_power_changed` : `params[0]` est lui-même un TABLEAU d'objets
 * `{"device":"nom","status":"on|off"}` -- piège différent de
 * rpc_fusionner_status/rpc_fusionner_instantane, où `params[0]` est un OBJET
 * de statut, pas un tableau. Rend false et ne touche RIEN à `*dest` si le
 * JSON est illisible, ou si l'enveloppe est hostile (`params` absent/pas un
 * tableau, `params[0]` absent ou pas lui-même un tableau) -- même politique
 * d'anomalie d'ENVELOPPE que rpc_fusionner_status.
 *
 * `dest` reçoit SEULEMENT les prises présentes dans cette notification
 * (pas une fusion avec un état antérieur : cette fonction est pure, elle ne
 * connaît pas le store -- c'est à l'appelant d'en faire ce qu'il veut,
 * typiquement une mise à jour ciblée par prise via
 * power_devices_maj_un() pour chacune). Mêmes règles `status`/`allumee`/
 * `connue`, mêmes défenses poison-par-élément, même troncature à
 * POWER_DEVICES_MAX/ignoré-si-trop-long que rpc_lire_power_devices
 * ci-dessus -- Moonraker peut en théorie regrouper plusieurs prises dans une
 * seule notification, même si un vrai Moonraker n'en pousse généralement
 * qu'une par message. */
bool rpc_lire_power_changed(power_devices_t *dest, const char *json, size_t longueur);

/* ------------------------------------------------------------------------
 * Console gcode (feature "Console gcode", tâche A -- store dédié
 * console_log.h, câblage WS laissé à la tâche B)
 * ---------------------------------------------------------------------- */

/* Extrait le texte d'une notification `notify_gcode_response` :
 * `{"method":"notify_gcode_response","params":["<texte>"]}` -- `params[0]`
 * est directement une STRING (contrairement à `notify_status_update`, où
 * `params[0]` est un OBJET de statut, voir rpc_fusionner_status ci-dessus :
 * piège différent, à ne pas confondre). Rend false SANS TOUCHER `dest` si le
 * JSON est illisible, ou si l'enveloppe est hostile (`params` absent/pas un
 * tableau, `params[0]` absent ou n'étant pas une string) -- même politique
 * d'anomalie d'ENVELOPPE que le reste de ce fichier.
 *
 * Klipper peut pousser une réponse arbitrairement longue (multi-lignes,
 * séparées par `\n` -- c'est à l'APPELANT de les découper en lignes
 * distinctes avant `console_log_ajouter()`, voir console_log.h) : `dest` est
 * donc rempli avec la même discipline UTF-8-safe que `rpc_lire_reponse()`
 * ci-dessus (jamais de coupe en plein milieu d'une séquence UTF-8
 * multi-octets, voir copier_texte_utf8_borne() dans moonraker_rpc.c) plutôt
 * que de faire échouer l'extraction entière pour une simple question de
 * taille de tampon -- une réponse tronquée reste plus utile qu'une réponse
 * perdue. Rend true dès que `params[0]` est bien une string, MÊME si `dest`
 * a dû être tronqué. */
bool rpc_lire_gcode_response(char *dest, size_t dest_n, const char *json, size_t n);

/* Extrait le champ `"method"` d'un message JSON-RPC entrant, tel quel (pour
 * le dispatch par NOM DE MÉTHODE de la tâche B, voir moonraker_ws.c). NOTE
 * (recherche faite avant d'ajouter cette fonction) : `rpc_classifier()`
 * ci-dessus lit déjà `"method"` en interne pour CLASSIFIER le message dans
 * `rpc_message_type_t`, mais ne l'expose nulle part à l'appelant -- aucune
 * fonction existante de ce fichier ne rend la chaîne `method` elle-même,
 * d'où cet ajout plutôt qu'une réutilisation.
 *
 * Rend false SANS TOUCHER `dest` si `dest`/`json` est NULL, `dest_n`/`n` vaut
 * 0, le JSON est illisible, si `"method"` est absent ou n'est pas une string,
 * OU si `dest_n` est trop petit pour la recevoir ENTIÈREMENT -- contrairement
 * à `rpc_lire_gcode_response()` ci-dessus, une méthode TRONQUÉE serait
 * dangereuse (elle pourrait se lire comme un préfixe d'une AUTRE méthode
 * connue et fausser le dispatch de l'appelant), donc jamais rendue comme un
 * succès -- même politique que `rpc_methode_commande()` plus bas. */
bool rpc_lire_methode(char *dest, size_t dest_n, const char *json, size_t n);

/* Taille de tampon suffisante pour toute méthode rendue par
 * rpc_methode_commande() ci-dessous : la plus longue
 * ("printer.emergency_stop", 23 octets) plus marge, jusqu'à l'octet nul —
 * même marge que MOONRAKER_CHEMIN_COMMANDE_MAX (moonraker_parse.h). */
#define RPC_METHODE_COMMANDE_MAX 32

/* Traduit une action commune (BACKEND_ACTION_PAUSE/REPRENDRE/ANNULER/URGENCE,
 * voir core/backend.h) en méthode JSON-RPC Moonraker -- des POINTS, jamais
 * de '/' (même piège que rpc_construire_abonnement() ci-dessus : Moonraker
 * dérive ses méthodes par ".".join(...), aucun alias HTTP-like). Pendant de
 * moonraker_chemin_commande() (moonraker_parse.h, chemins REST du sondage
 * HTTP) pour le transport WebSocket : mêmes QUATRE actions connues, une
 * méthode JSON-RPC plutôt qu'un chemin, copiée dans `methode` (qui doit
 * faire au moins RPC_METHODE_COMMANDE_MAX octets). Utilisée par
 * `backend_moonraker_commande()` (tâche 5) quand le WS est en ligne, avec
 * `rpc_construire_requete()` pour bâtir la requête complète -- ces quatre
 * actions ne prennent jamais de paramètres (voir backend.h), donc toujours
 * appelée avec `params_json = NULL`.
 *
 * Rend false SANS TOUCHER `methode` si `action` est NULL, si `methode` est
 * NULL, si `taille` vaut 0, ou si `action` ne correspond à aucune des
 * quatre actions connues (`BACKEND_ACTION_MACRO` y compris -- volontairement
 * REJETÉE ici, comme les trois autres actions inconnues : seule action du
 * registre à prendre des paramètres, elle ne rentre pas dans ce contrat "une
 * action -> une méthode sans argument". Tâche 6 : `backend_moonraker_commande()`
 * la traite À PART, avant d'appeler cette fonction, avec ses propres
 * paramètres `{"script":"<nom>"}` -- voir backend_moonraker.c) -- même
 * contrat que moonraker_chemin_commande(). */
bool rpc_methode_commande(const char *action, char *methode, size_t taille);
