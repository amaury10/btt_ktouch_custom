/* Fetch HTTP de la miniature gcode active (feature "Miniatures gcode", tâche
 * B) : GET `http://<hôte>:<port>/server/files/gcodes/<chemin>` vers un
 * tampon PSRAM borné, décodage/validation minimale (signature PNG), dépôt du
 * résultat dans miniature.h. ESP-ONLY (esp_http_client + FreeRTOS), exclu de
 * tout build PC -- exactement comme moonraker_ws.c (voir son commentaire de
 * tête).
 *
 * Tourne sur une tâche DÉDIÉE, créée pour l'occasion (patron ota.c --
 * ota_backup_btt()/ota_restaurer_btt() -- SAUF que ce fichier-ci ne bloque
 * PAS l'appelant sur un sémaphore : miniature_fetch_lancer() rend la main
 * immédiatement, comme demandé par la tâche (« ne PAS bloquer l'UI ni la
 * tâche WS »). L'appelant (moonraker_ws.c) est la tâche WS elle-même : un
 * GET HTTP peut prendre plusieurs secondes sur un LAN chargé, largement plus
 * que ce que cette tâche peut se permettre de perdre sans cesser de traiter
 * les messages WebSocket entrants. */
#pragma once

#include "backend.h"

/* Lance le fetch en tâche de fond pour `chemin_miniature` (chemin RELATIF à
 * la racine "gcodes" de Moonraker, tel que rendu par
 * miniature_construire_chemin(), voir moonraker_rpc.h) et dépose le résultat
 * sous `fichier_associe` (voir miniature_poser_prete()/miniature_poser_echec(),
 * qui rejettent silencieusement un dépôt dont le fichier associé ne
 * correspond plus à ce que le store suit -- l'appelant DOIT avoir déjà posé
 * miniature_poser_en_cours(fichier_associe) avant cet appel, comme le fait
 * moonraker_ws.c).
 *
 * Non bloquant : la tâche dédiée fait tout le travail réseau/allocation en
 * arrière-plan ; cette fonction ne fait que la créer et lui confier une
 * copie des paramètres (la tâche ne retient jamais un pointeur vers la pile
 * de l'appelant). Si la création de la tâche échoue (mémoire épuisée),
 * `miniature_poser_echec(fichier_associe)` est appelé SYNCHRONEMENT avant de
 * rendre la main -- l'échec reste visible côté store même si aucune tâche
 * n'a jamais tourné. `hote`/`chemin_miniature`/`fichier_associe` NULL : no-op
 * (aucun fetch lancé, aucun échec posé -- l'appelant n'a rien demandé de
 * valide). */
void miniature_fetch_lancer(const backend_hote_t *hote, const char *chemin_miniature,
                            const char *fichier_associe);
