/* Implémentation ESP de la façade plateforme.h : lit l'heure système, l'état
 * d'association WiFi et le RSSI réels du matériel.
 *
 * Seul fichier de core/ autorisé à inclure esp_wifi.h (voir le commentaire
 * d'en-tête de plateforme.h) : jamais compilé par le harnais de tests hôte
 * ni par le simulateur, seulement par le build firmware. */
#include "plateforme.h"

#include <time.h>

#include "esp_wifi.h"

/* Avant toute synchronisation horaire (pas de client SNTP dans ce firmware,
 * pas de pile RTC sur la K-Touch), time() rend une date en 1970 : afficher
 * "01:00" serait plus trompeur que ne rien afficher. 2020 est un seuil
 * arbitraire mais confortable, largement postérieur à toute date par défaut
 * d'un ESP32 non synchronisé et largement antérieur à aujourd'hui. */
#define PLATEFORME_ANNEE_MIN 2020

void plateforme_heure(plateforme_heure_t *sortie)
{
    time_t maintenant = time(NULL);
    struct tm decompose;
    localtime_r(&maintenant, &decompose);

    int annee = decompose.tm_year + 1900;
    if (annee < PLATEFORME_ANNEE_MIN) {
        sortie->heures = 0;
        sortie->minutes = 0;
        sortie->valide = false;
        return;
    }

    sortie->heures = (uint8_t)decompose.tm_hour;
    sortie->minutes = (uint8_t)decompose.tm_min;
    sortie->valide = true;
}

void plateforme_batterie(plateforme_batterie_t *sortie)
{
    /* La lecture de la tension de batterie de la K-Touch n'est pas connue à
     * ce stade (canal ADC, ratio de pont diviseur) : inventer une formule
     * afficherait un chiffre faux en permanence. Un champ invalide s'affiche
     * comme absent, ce qui est honnête (voir task-2-brief.md). */
    sortie->pourcentage = 0;
    sortie->en_charge = false;
    sortie->valide = false;
}

void plateforme_wifi(plateforme_wifi_t *sortie)
{
    wifi_ap_record_t info;
    esp_err_t erreur = esp_wifi_sta_get_ap_info(&info);
    if (erreur != ESP_OK) {
        /* Cas courant, pas une panne : pas encore associé, ou tentative en
         * cours (wifi.c relance esp_wifi_connect() lui-même). Ne pas
         * journaliser ici : la barre d'état interroge cette fonction à
         * chaque rafraîchissement, et un message par cycle noierait /log
         * sans rien apprendre de plus que ce que `associe = false` dit déjà
         * à l'écran. */
        sortie->associe = false;
        sortie->rssi = 0;
        sortie->barres = 0;
        return;
    }

    sortie->associe = true;
    sortie->rssi = info.rssi;
    sortie->barres = plateforme_wifi_barres(info.rssi);
}
