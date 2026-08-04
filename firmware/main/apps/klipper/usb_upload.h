/* usb_upload.h — cadrage PUR (aucun réseau, aucun accès fichier) de l'upload
 * multipart/form-data d'un .gcode depuis la clé USB vers Moonraker
 * (`POST /server/files/upload`, champs `root=gcodes`, `print=true`,
 * `file=<contenu>`). C'est la tâche A de la feature « Impression depuis USB »
 * (voir docs/superpowers/specs/2026-08-04-usb-impression-design.md) : cette
 * couche ne fait QUE construire les octets à envoyer AVANT et APRÈS le
 * contenu du fichier (préambule/trailer) et calculer le Content-Length total
 * -- le streaming réel (pt_usb_read -> esp_http_client_write, tâche B,
 * ESP-only) copie ensuite le fichier lui-même entre les deux, jamais chargé
 * ici ni en RAM.
 *
 * Style des helpers purs du dépôt (voir json_util.h, klipper_gcode.h) :
 * fonctions sans état, contrat retour façon snprintf() (le nombre d'octets
 * qui AURAIENT été écrits si `dest` avait été assez grand ; comparer au
 * paramètre de taille pour détecter une troncature). `dest`/`n` peuvent être
 * NULL/0 pour sonder la longueur nécessaire sans rien écrire (même usage que
 * snprintf(NULL, 0, ...)). */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Vrai si `nom` se termine par `.gcode`, `.gco` ou `.g` (insensible à la
 * casse, ex. `.GCODE`/`.Gco` acceptés). `nom == NULL` -> false. Ne regarde
 * QUE l'extension : ne valide ni l'existence du fichier ni son contenu. */
bool usb_est_gcode(const char *nom);

/* Construit tout le corps multipart/form-data AVANT les octets du fichier
 * lui-même : trois parts `root`=`gcodes`, `print`=`true`, puis l'en-tête de
 * la part `file` (Content-Disposition + filename nettoyé + Content-Type +
 * la ligne vide finale qui sépare les en-têtes du corps de la part) --
 * l'appelant écrit les octets du fichier juste après ce préambule, puis
 * usb_upload_trailer() pour clore proprement le multipart :
 *
 *   --<boundary>\r\n
 *   Content-Disposition: form-data; name="root"\r\n
 *   \r\n
 *   gcodes\r\n
 *   --<boundary>\r\n
 *   Content-Disposition: form-data; name="print"\r\n
 *   \r\n
 *   true\r\n
 *   --<boundary>\r\n
 *   Content-Disposition: form-data; name="file"; filename="<nettoyé>"\r\n
 *   Content-Type: application/octet-stream\r\n
 *   \r\n
 *
 * `filename` est inséré dans un en-tête HTTP : tout `"`, `\r` et `\n` y est
 * RETIRÉ (pas remplacé -- défense injection d'en-tête, un nom de fichier ne
 * les contient normalement jamais sur une clé FAT mais rien ne l'empêche
 * structurellement) avant insertion. `boundary`/`filename` NULL sont traités
 * comme une chaîne vide (jamais de déréférencement NULL).
 *
 * Contrat retour façon snprintf() : rend le nombre d'octets qui AURAIENT été
 * écrits si `dest` avait été assez grand ; borne stricte, jamais de
 * débordement de `dest` (au plus `n - 1` octets utiles + le '\0' final).
 * `dest`/`n` peuvent être NULL/0 pour sonder la longueur sans rien écrire. */
size_t usb_upload_preambule(char *dest, size_t n, const char *boundary,
                            const char *filename);

/* Clôt le multipart après les octets du fichier :
 *   \r\n--<boundary>--\r\n
 * `boundary` NULL traité comme une chaîne vide. Même contrat retour façon
 * snprintf() que usb_upload_preambule() ci-dessus. */
size_t usb_upload_trailer(char *dest, size_t n, const char *boundary);

/* Somme des trois segments du corps streamé (préambule + octets du fichier +
 * trailer), pour l'en-tête HTTP Content-Length de l'upload -- Moonraker (et
 * esp_http_client en émission) ont besoin de connaître la taille totale à
 * l'avance, avant que le corps ne soit effectivement écrit en streaming. */
size_t usb_upload_content_length(size_t preambule_len, size_t taille_fichier,
                                 size_t trailer_len);
