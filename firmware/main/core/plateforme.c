/* Conversion pure du RSSI (dBm) en nombre de barres affichées, partagée par
 * l'implémentation ESP (plateforme_esp.c) et l'implémentation simulateur
 * (simulateur/plateforme_sim.c) pour qu'un même signal produise le même
 * nombre de barres des deux côtés. Aucune dépendance ESP-IDF ni FreeRTOS :
 * ce fichier reste, comme le reste de core/, compilable par le harnais de
 * tests hôte et par le simulateur SDL. */
#include "plateforme.h"

uint8_t plateforme_wifi_barres(int8_t rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}
