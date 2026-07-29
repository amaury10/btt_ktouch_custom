/* Client WebSocket Moonraker (jalon 3a, tâche 5) : ESP-ONLY (esp_websocket_client
 * + FreeRTOS), exclu de tout build PC (host-test, simulateur) -- voir
 * moonraker_boite.h pour la partie portable dont ce fichier est l'unique
 * appelant côté cible.
 *
 * Tourne dans sa PROPRE tâche (celle d'esp_websocket_client) : elle ne
 * touche JAMAIS `etat_store_*`, `boucle_*`, ni LVGL (spec §4). Le seul canal
 * de sortie vers le reste du firmware est la boîte aux lettres interne
 * (drainée via moonraker_ws_drainer() ci-dessous, jamais exposée
 * directement -- c'est CE fichier qui possède le verrou, pas
 * backend_moonraker.c, voir le commentaire de moonraker_boite.h sur "le
 * verrou est fourni par l'appelant côté ESP").
 *
 * Un seul client tourne à la fois dans le socle (même hypothèse que
 * backend_moonraker.c pour g_hote/g_client) : ce module est un singleton de
 * fichier, pas une instance à part. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "backend.h"
#include "etat_klipper.h"

/* Démarre le client WS vers `ws://<hote->adresse>:<hote->port>/websocket`
 * (crochets IPv6 comme moonraker_construire_url() dans backend_moonraker.c).
 * NON bloquant : la connexion elle-même, l'identification et l'abonnement se
 * font en tâche de fond ; `moonraker_ws_en_ligne()` rend false tant qu'ils
 * n'ont pas abouti. Rend ESP_FAIL si esp_websocket_client_init() échoue. */
esp_err_t moonraker_ws_demarrer(const backend_hote_t *hote);

/* Arrête le client et libère ses ressources. Idempotent (sans effet si le
 * client n'a jamais démarré ou est déjà arrêté). */
void moonraker_ws_arreter(void);

/* true si la connexion WebSocket est établie -- c'est CE booléen qui décide,
 * dans backend_moonraker_rafraichir()/commande(), si le socle passe par le
 * WS ou retombe sur le HTTP existant. Ne dit rien de Klippy lui-même : une
 * machine dont le MCU vient de s'arrêter (thermal runaway...) garde un WS
 * parfaitement connecté à Moonraker -- voir moonraker_ws_klippy_pret(). */
bool moonraker_ws_en_ligne(void);

/* true si, au meilleur de la connaissance de ce module, Klippy est
 * utilisable -- optimiste par défaut à chaque (re)connexion (une nouvelle
 * identification + un nouvel abonnement viennent d'être envoyés, rien ne
 * dit encore le contraire), rendu false par une notification
 * notify_klippy_disconnected/notify_klippy_shutdown (RPC_MSG_KLIPPY_DECONNECTE,
 * voir moonraker_rpc.h), rendu true à nouveau par notify_klippy_ready
 * (RPC_MSG_KLIPPY_READY). C'est la distinction que backend_moonraker.c doit
 * préserver (spec §4) : WS connecté ET Klippy prêt = succès de cycle ; WS
 * connecté mais Klippy down = échec, sans jamais retomber sur le HTTP dans
 * ce cas précis (l'échec doit être visible, pas masqué par un repli). */
bool moonraker_ws_klippy_pret(void);

/* Draine le dernier état complet fusionné (voir moonraker_boite.h) --
 * false si rien de neuf depuis le dernier drain, `*sortie` alors intact.
 * Thread-safe : le verrou est posé ICI (contrairement à boite_drainer() de
 * moonraker_boite.h, volontairement dépourvue de verrou). */
bool moonraker_ws_drainer(etat_klipper_t *sortie);

/* Envoie une commande RPC corrélée et ATTEND sa résolution -- réponse ou
 * timeout -- en interrogeant le corrélateur par un court sondage (PAS en
 * bloquant la tâche WS elle-même, voir le commentaire de tête de
 * moonraker_ws.c pour le détail du mécanisme). Prévu pour être appelé
 * UNIQUEMENT depuis boucle_traiter_commandes() (core/boucle.c), qui traite
 * les commandes une par une, jamais en concurrence avec elle-même -- un
 * appel concurrent à un second déjà en cours rend ESP_ERR_INVALID_STATE
 * (garde défensive, pas un chemin normal).
 *
 * `params_json` suit le contrat de rpc_construire_requete() (NULL = clé
 * "params" omise). Rend :
 *   - ESP_OK avec `*succes` rempli (et `erreur_texte` si Klipper a rejeté la
 *     commande) si une réponse JSON-RPC est arrivée à temps -- que Klipper
 *     ait accepté ou refusé, c'est un résultat RÉEL, jamais inventé ;
 *   - ESP_ERR_TIMEOUT si rien n'est arrivé sous `timeout_ms` ;
 *   - ESP_FAIL si le WS n'est pas connecté, si la requête n'a pas pu être
 *     construite, ou si l'envoi lui-même a échoué ;
 *   - ESP_ERR_INVALID_STATE si un appel est déjà en cours (voir ci-dessus).
 *
 * `erreur_texte`/`taille_erreur` peuvent être NULL/0 (voir rpc_lire_reponse()). */
esp_err_t moonraker_ws_commande(const char *methode, const char *params_json,
                                 uint32_t timeout_ms, bool *succes,
                                 char *erreur_texte, size_t taille_erreur);

/* Nombre de tentatives de reconnexion depuis le dernier moonraker_ws_demarrer()
 * -- exposé pour le journal throttlé de backend_moonraker.c (le journal de
 * CE fichier reste interne, voir son commentaire de tête sur le risque de
 * déluge déjà documenté dans backend_moonraker.c). */
uint32_t moonraker_ws_compteur_reconnexions(void);
