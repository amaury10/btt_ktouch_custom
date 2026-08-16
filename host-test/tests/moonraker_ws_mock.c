/* Mock host-test/simulateur de la partie de moonraker_ws.h dont dépend un
 * ÉCRAN -- aujourd'hui la seule moonraker_ws_demander_bobines() (feature
 * Spoolman, 2026-08-15 : bouton Refresh). AUCUN vrai WebSocket ici
 * (esp_websocket_client n'existe pas sur PC, voir
 * firmware/main/apps/klipper/moonraker_ws.c pour la vraie implémentation
 * ESP-only) : même politique que wifi_mock.c/usb_upload_http_mock.c, c'est
 * ce qui permet à ecran_spoolman.c d'être compilé UNE SEULE FOIS, ici comme
 * sur la cible, plutôt que dupliqué derrière un #ifdef.
 *
 * Le mock COMPTE les appels au lieu de les ignorer : c'est la seule façon de
 * prouver côté host que le bouton Refresh déclenche réellement quelque
 * chose (un no-op muet passerait le test même si le rappel n'était pas
 * câblé). */
#include "moonraker_ws_mock.h"

#include "moonraker_ws.h"

static unsigned g_demandes_bobines;

void moonraker_ws_demander_bobines(void)
{
    g_demandes_bobines++;
}

unsigned moonraker_ws_mock_demandes_bobines(void)
{
    return g_demandes_bobines;
}

void moonraker_ws_mock_reinitialiser(void)
{
    g_demandes_bobines = 0;
}
