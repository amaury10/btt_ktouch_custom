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
 * faire perdre les autres champs, légitimes, du même message. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "etat_klipper.h"

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

/* Requête `printer.objects.subscribe` (nom de méthode JSON-RPC Moonraker :
 * des POINTS, jamais de '/' — voir le commentaire de tête de
 * rpc_construire_abonnement() dans moonraker_rpc.c pour l'histoire complète
 * de cette correction) avec les objets dont l'état v2 a besoin (toolhead,
 * gcode_move, extruder..extruder7, heater_bed, fan, print_stats,
 * virtual_sdcard, webhooks). speed_factor/extrude_factor sont portés par
 * gcode_move, déjà dans la liste — pas d'entrée séparée. Même contrat de
 * tampon que rpc_construire_requete() ci-dessus. */
bool rpc_construire_abonnement(char *sortie, size_t taille, uint32_t id);

typedef enum {
    RPC_MSG_REPONSE,          /* result ou error, avec id numérique */
    RPC_MSG_STATUS_UPDATE,    /* notify_status_update */
    RPC_MSG_KLIPPY_READY,     /* notify_klippy_ready */
    RPC_MSG_KLIPPY_DECONNECTE,/* notify_klippy_disconnected */
    RPC_MSG_AUTRE,            /* notification reconnue comme telle mais ignorée */
    RPC_MSG_INVALIDE,         /* JSON illisible, ou ni réponse ni notification */
} rpc_message_type_t;

/* Classifie un message entrant sans le consommer (ne modifie aucun état).
 * `id_sortie` (si non NULL) reçoit l'id UNIQUEMENT quand le résultat est
 * RPC_MSG_REPONSE (mis à 0 dans tous les autres cas, y compris invalide).
 * Une réponse sans id numérique (`result`/`error` présent mais `id` absent
 * ou non numérique) est classée RPC_MSG_INVALIDE : un id est la seule
 * façon de corréler la réponse à la requête qui l'a provoquée, une réponse
 * qu'on ne peut pas corréler n'est pas exploitable. */
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
 * flux_pct en %, homing_origin[2] en mm -> babystep_z_um arrondi au plus
 * proche, absolute_coordinates -> deplacement_absolu), `extruder`..
 * `extruder7` (index tiré du NOM de l'objet ; hors bornes -> objet ignoré,
 * reste du message appliqué normalement), `heater_bed`, `fan` (mappé sur
 * ventilateurs[0], seul ventilateur nommément connu du protocole),
 * `print_stats` (state/filename, plus impression_en_cours/en_pause
 * recalculés), `virtual_sdcard` (progress -> progression). `nb_extrudeurs`
 * est recalculé après fusion comme le compte de `presente` sur les 8
 * emplacements, pas seulement ceux touchés par ce message. */
bool rpc_fusionner_status(etat_klipper_t *etat, const char *json, size_t longueur);

/* Extrait résultat/erreur d'une réponse JSON-RPC déjà corrélée par
 * l'appelant (voir rpc_classifier ci-dessus pour l'id). Rend false si le
 * JSON est illisible ou ne contient ni "result" ni "error". Sinon rend
 * true et `*succes` indique lequel des deux est présent : true pour
 * "result", false pour "error" (le message Klipper de l'erreur, s'il
 * existe, est recopié dans `erreur_texte`, tronqué à `taille_erreur` SANS
 * jamais couper au milieu d'une séquence UTF-8 multi-octets — un message
 * Klipper peut contenir des caractères accentués ou "°C" ; une coupe
 * aveugle produirait une chaîne mal formée en fin de tampon). Si `*succes`
 * est true, ou si l'erreur n'a pas de champ "message" exploitable,
 * `erreur_texte` reçoit une chaîne vide. `erreur_texte`/`taille_erreur`
 * peuvent être omis (NULL/0) si l'appelant ne veut pas le texte. */
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
