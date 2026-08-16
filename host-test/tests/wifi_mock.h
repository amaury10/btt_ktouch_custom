/* Façade WiFi mockée pour host-test ET simulateur.
 *
 * wifi.c dépend d'esp_wifi et n'est PAS compilé hors ESP-IDF : ce fichier
 * fournit un double des fonctions wifi_* (voir firmware/main/wifi.h) que
 * l'écran de réglages WiFi consomme, plus quelques crochets d'introspection
 * pour les tests. wifi_scanner() y est SYNCHRONE (aucun matériel, aucun fil) :
 * l'écran est structuré pour que cela marche aussi bien qu'un balayage réel
 * bloquant (voir ecran_reglages_wifi.c). */
#pragma once

#include "wifi.h"

/* Remet le mock à zéro : oublie le dernier wifi_reconfigurer() enregistré,
 * remet l'état de reconfiguration à INACTIF et l'état courant à « connecté à
 * MaBox_5G ». À appeler entre deux scénarios de test. */
void wifi_mock_reset(void);

/* Dernier couple (ssid, pass) passé à wifi_reconfigurer(), et nombre d'appels
 * depuis le dernier reset. `pass` est la chaîne exacte reçue (vide pour un
 * réseau ouvert). */
const char *wifi_mock_dernier_ssid(void);
const char *wifi_mock_dernier_pass(void);
int         wifi_mock_appels_reconfigurer(void);

/* Force l'état renvoyé par wifi_reconfig_etat() (par défaut, wifi_reconfigurer
 * pose EN_COURS). Permet à un test de simuler REUSSI / ECHOUE, etc. */
void wifi_mock_definir_reconfig_etat(wifi_reconfig_etat_t etat);

/* Force ce que wifi_etat() renvoie (SSID courant + connecté). */
void wifi_mock_definir_etat(const char *ssid, bool connecte);
