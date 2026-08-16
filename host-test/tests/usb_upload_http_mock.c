/* Mock host-test de usb_upload_http.h -- AUCUNE vraie E/S réseau (esp_http_client
 * n'existe pas sur PC, voir firmware/main/apps/klipper/usb_upload_http.c pour
 * la vraie implémentation ESP-only) : maintient le MÊME contrat de
 * signatures/état/singleton que celle-ci, exactement comme wifi_mock.c le
 * fait pour wifi.h -- c'est ce qui permet à ecran_usb.c (feature "Impression
 * depuis USB", tâche B) d'être compilé UNE SEULE FOIS, ici comme sur la
 * cible, plutôt que dupliqué. `usb_upload_http_demarrer()` ici ne fait
 * jamais réellement d'upload : il bascule directement l'état à
 * USB_UPLOAD_HTTP_SUCCES (best-effort, ce mock ne prétend PAS exercer la
 * logique réseau elle-même -- celle-ci ne vit QUE dans l'implémentation ESP,
 * jamais compilée ici). */
#include "usb_upload_http.h"

#include <string.h>

static usb_upload_http_progression_t g_etat = {
    .etat = USB_UPLOAD_HTTP_INACTIF,
    .envoyes = 0,
    .total = 0,
    .message = "",
};

bool usb_upload_http_demarrer(const char *chemin_usb, size_t taille_fichier)
{
    if (chemin_usb == NULL || chemin_usb[0] == '\0') {
        return false;
    }
    if (g_etat.etat == USB_UPLOAD_HTTP_EN_COURS) {
        return false; /* même refus singleton que l'implémentation ESP réelle */
    }
    g_etat.etat = USB_UPLOAD_HTTP_SUCCES; /* pas de réseau hôte-test : succès immédiat simulé */
    g_etat.envoyes = taille_fichier;
    g_etat.total = taille_fichier;
    g_etat.message[0] = '\0';
    return true;
}

void usb_upload_http_lire(usb_upload_http_progression_t *dest)
{
    if (dest == NULL) {
        return;
    }
    *dest = g_etat;
}

bool usb_upload_http_en_cours(void)
{
    return g_etat.etat == USB_UPLOAD_HTTP_EN_COURS;
}
