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

/* Tache 5 (jalon OTA firmware) : COMMIT reel -- recoit le corps d'une requete
 * POST (`req`) EN FLUX, sur la tache appelante (la tache httpd : httpd_req_recv()
 * n'est valide que sur la tache proprietaire de la requete, contrairement au
 * travail flash de ota_backup_btt() ci-dessus, qui ne touche jamais `req` et
 * peut donc tourner sur une tache dediee), et l'ecrit reellement dans le slot
 * OTA inactif (esp_ota_get_next_update_partition() -- jamais le slot en cours
 * d'execution).
 *
 * GARDE DE SURETE (invariant le plus important de tout ce fichier) : tant que
 * la cible est app0 (le slot qui porte encore le firmware BTT d'origine, ce
 * qui reste vrai jusqu'au tout premier commit reussi de ce module) ET qu'aucun
 * commit precedent n'a deja ecrase app0 (drapeau NVS persistant), un backup
 * BTT valide (ota_backup_etat() == OTA_BACKUP_VALIDE) est EXIGE avant
 * d'appeler esp_ota_begin() -- sinon REFUS immediat, aucune ecriture flash.
 * Sans cette garde, la toute premiere mise a jour ecraserait le seul
 * exemplaire du firmware d'origine avant meme qu'il ait ete sauvegarde --
 * irrecuperable sur un appareil sans port serie.
 *
 * Sequence apres la garde : esp_ota_begin() -> reception par blocs (magic
 * 0xE9 verifie sur le tout PREMIER bloc avant d'ecrire quoi que ce soit) ->
 * esp_ota_write() par bloc -> esp_ota_end() (SEULE porte de validation de
 * l'image ; echoue => esp_ota_abort() deja fait, PAS de set_boot) ->
 * esp_ota_set_boot_partition() (jamais appele si esp_ota_end() a echoue) ->
 * si la cible etait app0, pose du drapeau NVS (les commits SUIVANTS visant
 * app0 n'exigeront alors plus de backup, BTT n'existant alors plus) ->
 * rescue_arm() (meme filet de sauvetage que le demarrage normal : si la
 * nouvelle image ne rejoint jamais le reseau, rescue.c rebascule seul vers le
 * slot precedent). N'appelle JAMAIS esp_restart() elle-meme : c'est a
 * l'appelant (web.c) de repondre au client HTTP d'abord, puis de redemarrer
 * apres un court delai -- meme discipline que /revert.
 *
 * `msg`/`msg_taille` : message humain toujours ecrit tant que msg != NULL et
 * msg_taille > 0 (succes avec le nombre d'octets ecrits, refus de la garde
 * avec l'invite explicite a lancer /backup-btt, ou description de l'erreur
 * de reception/ecriture/validation), y compris en cas d'echec. Rend ESP_OK
 * SEULEMENT si l'image a ete ecrite, validee par esp_ota_end() et le
 * demarrage programme dessus. */
esp_err_t ota_appliquer_flux(httpd_req_t *req, char *msg, size_t msg_taille);

/* Tache 6 (jalon OTA firmware, dernier) : RESTAURATION BTT -- l'assurance qui
 * rend tout le reste de ce module reversible. Relit la sauvegarde spiffs
 * (en-tete puis image, voir ota_backup_etat()/ota_backup_btt() plus haut) et
 * l'ecrit dans le slot OTA inactif (esp_ota_get_next_update_partition() --
 * jamais le slot en cours d'execution) via le MEME chemin qu'un commit OTA
 * normal : esp_ota_begin() -> ecriture par blocs -> esp_ota_end() (SEULE
 * porte de validation ; BTT est une image applicative ESP comme une autre de
 * son point de vue, donc elle la valide de la meme facon) -> si et seulement
 * si esp_ota_end() reussit, esp_ota_set_boot_partition() -> rescue_arm()
 * (meme filet de sauvetage que le demarrage normal et que le commit OTA).
 * N'appelle jamais esp_restart() elle-meme : c'est a l'appelant (web.c) de
 * repondre au client HTTP d'abord, meme discipline que /revert et /ota.
 *
 * GARDE DE SURETE : exige ota_backup_etat() == OTA_BACKUP_VALIDE AVANT
 * d'appeler quoi que ce soit d'autre -- REFUS immediat sinon, aucune
 * ecriture flash, aucun esp_ota_begin(). Contrairement a la garde de
 * ota_appliquer_flux() ci-dessus (qui protege la TOUTE PREMIERE ecriture
 * dans app0 tant qu'il porte encore BTT), cette garde-ci n'est pas
 * conditionnee a la cible ou au drapeau NVS btt_ecrase : une restauration
 * sans sauvegarde fiable n'a de toute facon aucun sens (rien a restaurer).
 * Ne pose ni ne lit le drapeau NVS btt_ecrase : la restauration ne change
 * rien a l'HISTORIQUE d'ecrasement d'app0, seulement a ce qui s'y trouve
 * maintenant -- ce drapeau reste la propriete exclusive de
 * ota_appliquer_flux().
 *
 * Sur toute erreur en cours de flux (lecture spiffs, ecriture) :
 * esp_ota_abort(), message, PAS de set_boot -- meme discipline que
 * ota_appliquer_flux().
 *
 * `msg`/`msg_taille` : message humain toujours ecrit tant que msg != NULL et
 * msg_taille > 0 (succes avec le nombre d'octets ecrits, refus de la garde
 * si la sauvegarde n'est pas valide, ou description de l'erreur de
 * lecture/ecriture/validation), y compris en cas d'echec. Rend ESP_OK
 * SEULEMENT si l'image sauvegardee a ete ecrite, validee par esp_ota_end()
 * et le demarrage programme dessus. */
esp_err_t ota_restaurer_btt(char *msg, size_t msg_taille);
