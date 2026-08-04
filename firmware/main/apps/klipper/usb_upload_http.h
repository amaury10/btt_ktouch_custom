/* Upload HTTP streamé d'un .gcode depuis la clé USB vers Moonraker
 * (`POST /server/files/upload`, `root=gcodes`, `print=true` -- voir
 * usb_upload.h pour le cadrage multipart pur, tâche A). ESP-only (esp_http_client
 * + FreeRTOS) : jamais compilé par host-test, voir firmware/main/CMakeLists.txt.
 *
 * Contrat asynchrone, PAS synchrone comme ota_backup_btt()/ota_restaurer_btt()
 * (ota.h) : usb_upload_http_demarrer() lance une tâche dédiée et rend la main
 * immédiatement -- un .gcode peut faire plusieurs Mo, l'écran doit pouvoir
 * afficher une barre de progression pendant l'envoi plutôt que de bloquer la
 * tâche LVGL (interdit, voir ecran.h) jusqu'à la fin. La progression/le
 * résultat sont exposés via usb_upload_http_lire(), relue par
 * ecran_usb.c::mettre_a_jour() à chaque pompage. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    USB_UPLOAD_HTTP_INACTIF = 0, /* aucun upload n'a encore été demandé */
    USB_UPLOAD_HTTP_EN_COURS,
    USB_UPLOAD_HTTP_SUCCES,
    USB_UPLOAD_HTTP_ECHEC,
} usb_upload_http_etat_t;

#define USB_UPLOAD_HTTP_MESSAGE_MAX 96

typedef struct {
    usb_upload_http_etat_t etat;
    size_t envoyes; /* octets déjà écrits sur le socket (préambule+fichier+trailer) */
    size_t total;   /* Content-Length complet (usb_upload_content_length()) */
    char   message[USB_UPLOAD_HTTP_MESSAGE_MAX]; /* raison de l'échec ; vide sinon */
} usb_upload_http_progression_t;

/* Démarre l'upload de `chemin_usb` (chemin complet sous /usb, ex.
 * "/usb/piece.gcode" -- typiquement une entrée du store usb_fichiers.h) vers
 * Moonraker. Le nom de fichier envoyé dans le multipart est le dernier
 * segment de `chemin_usb` (après le dernier '/'). `taille_fichier` est la
 * taille CONNUE du fichier (propagée depuis pt_usb_dir_entry_t.size par le
 * scan de app_main.c/usb_fichiers.h) -- jamais relue ici (pas de stat()
 * supplémentaire), nécessaire pour calculer Content-Length AVANT le premier
 * octet écrit (voir usb_upload_content_length()).
 *
 * Hôte/port Moonraker lus depuis reglages_hote() (les mêmes réglages que le
 * backend Moonraker principal, voir core/reglages.h) -- jamais une seconde
 * source de configuration.
 *
 * Rend FAUX sans rien démarrer si `chemin_usb` est NULL/vide, si un upload
 * est DÉJÀ en cours (singleton, même politique que clavier.h/confirmation.h
 * -- un second appel n'écrase jamais un envoi en cours), ou si la tâche
 * dédiée n'a pas pu être créée (mémoire épuisée -- l'état bascule alors
 * directement à USB_UPLOAD_HTTP_ECHEC, lisible via usb_upload_http_lire()).
 * Rend VRAI dès que la tâche a été créée -- ne dit RIEN sur le résultat final
 * de l'upload, qui n'est connu qu'en relisant usb_upload_http_lire() plus
 * tard. */
bool usb_upload_http_demarrer(const char *chemin_usb, size_t taille_fichier);

/* Copie l'état/la progression courants dans `*dest` (thread-safe -- lu depuis
 * la tâche LVGL pendant que la tâche d'upload dédiée les modifie). `dest`
 * NULL = no-op. Avant le tout premier usb_upload_http_demarrer(), rend
 * `etat == USB_UPLOAD_HTTP_INACTIF`, `envoyes == 0`, `total == 0`, message
 * vide. */
void usb_upload_http_lire(usb_upload_http_progression_t *dest);

/* Vrai si `etat == USB_UPLOAD_HTTP_EN_COURS` -- même test que le refus
 * interne de usb_upload_http_demarrer(), exposé pour que l'écran désactive
 * son bouton d'envoi SANS tenter un second démarrage qu'il sait déjà voué à
 * être refusé. */
bool usb_upload_http_en_cours(void);
