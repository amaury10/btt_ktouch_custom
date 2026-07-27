/* Implémentation simulateur de la façade plateforme.h.
 *
 * L'heure vient de l'horloge système du PC : rien à simuler, un affichage
 * correct sur cette valeur-là ne prouve rien de plus qu'une lecture réussie
 * de time(). Le WiFi et la batterie, eux, sont des valeurs SYNTHÉTIQUES
 * figées (aucun matériel réel derrière), choisies pour que la tâche 4
 * puisse dessiner le cas nominal de la barre d'état : associé avec un bon
 * signal, batterie disponible et non en charge. */
#include "plateforme.h"

#include <time.h>

void plateforme_heure(plateforme_heure_t *sortie)
{
    time_t maintenant = time(NULL);
    struct tm decompose;
    localtime_r(&maintenant, &decompose);

    sortie->heures = (uint8_t)decompose.tm_hour;
    sortie->minutes = (uint8_t)decompose.tm_min;
    sortie->valide = true;
}

void plateforme_batterie(plateforme_batterie_t *sortie)
{
    /* Synthétique : 76 %, pas en charge, valide. */
    sortie->pourcentage = 76;
    sortie->en_charge = false;
    sortie->valide = true;
}

void plateforme_wifi(plateforme_wifi_t *sortie)
{
    /* Synthétique : associé, -58 dBm (3 barres selon les seuils de
     * plateforme_wifi_barres()). */
    sortie->associe = true;
    sortie->rssi = -58;
    sortie->barres = plateforme_wifi_barres(sortie->rssi);
}
