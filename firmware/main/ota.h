#pragma once

/* Sauvegarde de l'image BTT (app0, le firmware d'origine) vers la partition
 * spiffs, RAW (en-tete ota_backup_entete_t puis l'image), avec verification
 * SHA-256 apres relecture depuis la flash.
 *
 * Garantie de surete de ce module : app0 est UNIQUEMENT LU. La seule
 * ecriture flash de ce fichier cible la partition spiffs (subtype
 * ESP_PARTITION_SUBTYPE_DATA_SPIFFS, inutilisee par ce firmware -- voir
 * partitions.csv) -- jamais un slot app (app0 NI app1). Rien ici n'appelle
 * esp_ota_begin/esp_ota_write/esp_ota_set_boot_partition : c'est la tache
 * de ota.c pour le commit OTA (jalon suivant), qui prendra ses propres
 * garde-fous, pas de ce fichier. Cette sauvegarde est donc reexecutable a
 * volonte, sans le moindre risque de rendre l'appareil non demarrable. */

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

typedef enum {
    OTA_BACKUP_ABSENT,   /* aucune sauvegarde valide en tete de spiffs (magic absent/errone) */
    OTA_BACKUP_VALIDE,   /* en-tete present, SHA-256 relu concorde */
    OTA_BACKUP_CORROMPU, /* en-tete present mais SHA-256 relu ne correspond pas (ou taille aberrante) */
} ota_backup_etat_t;

/* Relit l'en-tete puis les donnees depuis spiffs, recalcule le SHA-256 de
   l'image sauvegardee et le compare a celui stocke dans l'en-tete. Ne
   modifie jamais rien (lecture seule). Cout : un passage complet sur les
   octets sauvegardes (streaming par blocs, jamais l'image entiere en RAM) --
   voir ota.c. Utilisee par /status et /backup-btt (web.c). */
ota_backup_etat_t ota_backup_etat(void);

/* Copie l'image app0 (firmware BTT d'origine) vers la partition spiffs,
   BRUTE : en-tete ota_backup_entete_t (magic/taille=octets d'image/sha256)
   puis les octets de l'image, ecrits par blocs pendant que le SHA-256 de
   l'image s'accumule au fil de l'eau -- jamais l'image complete (4,5 Mio) en
   RAM. Une fois l'ecriture terminee, RELIT independamment depuis spiffs
   (via ota_backup_etat()) pour prouver que la flash contient reellement ce
   qui vient d'etre demande, pas seulement que le calcul fait pendant
   l'ecriture s'accorde avec lui-meme.

   `msg`/`msg_taille` : message humain toujours ecrit tant que msg != NULL et
   msg_taille > 0 (succes avec le SHA-256 en hexa, ou description de
   l'erreur), y compris en cas d'echec. Rend ESP_OK seulement si la
   relecture confirme OTA_BACKUP_VALIDE.

   N'ECRIT JAMAIS dans un slot app : app0 est seulement lu. Le travail flash
   (lecture app0 + effacement/ecriture spiffs + relecture, quelques secondes
   pour ~4,5 Mio) est delegue a une tache dediee creee pour l'occasion (meme
   motif que rescue_switch_now() dans rescue.c) : cet appel BLOQUE
   l'appelant jusqu'a la fin (via un semaphore), mais l'appelant -- la tache
   httpd -- ne porte jamais lui-meme la boucle d'E/S flash sur sa propre
   pile, et laisse l'ordonnanceur executer les taches IDLE pendant l'attente
   (contrairement a une boucle synchrone qui monopoliserait un coeur assez
   longtemps pour risquer de declencher le chien de garde des taches). */
esp_err_t ota_backup_btt(char *msg, size_t msg_taille);

/* Tache 4 (jalon OTA firmware) : DRY-RUN de reception d'une image applicative
 * -- recoit le corps d'une requete POST (`req`) EN FLUX, calcule son SHA-256
 * a la volee (mbedtls, jamais l'image entiere en RAM/flash), verifie le
 * magic ESP (premier octet == 0xE9) et que la taille recue tient dans la
 * partition ciblee par une future mise a jour (esp_ota_get_next_update_partition,
 * appelee UNIQUEMENT pour lire sa taille -- aucune ecriture, aucun esp_ota_begin).
 * Si `sha_attendu_hex` est non NULL et non vide (64 caracteres hexadecimaux,
 * voir ota_hex_vers_sha256() dans ota_image.h), compare le SHA calcule au SHA
 * fourni.
 *
 * GARANTIE DE SURETE : cette fonction N'APPELLE JAMAIS esp_ota_begin(),
 * esp_ota_write(), esp_ota_set_boot_partition(), ni aucune primitive
 * esp_partition_erase/write -- lecture/reception/hachage SEULEMENT. Rien de
 * ce que recoit cet appel n'est jamais ecrit en flash. Le commit OTA reel
 * (jalon suivant) sera une fonction DISTINCTE, avec ses propres garde-fous.
 *
 * `msg`/`msg_taille` : message humain toujours ecrit tant que msg != NULL et
 * msg_taille > 0 -- le SHA-256 calcule (hexa) suivi du verdict ("image
 * valide", "magic invalide", "taille hors bornes", "SHA ne correspond pas",
 * ou une erreur de reception/partition), y compris en cas d'echec. Rend
 * ESP_OK seulement si l'image est structurellement valide ET (si fourni) que
 * le SHA fourni correspond. */
esp_err_t ota_verifier_flux(httpd_req_t *req, const char *sha_attendu_hex, char *msg, size_t msg_taille);
