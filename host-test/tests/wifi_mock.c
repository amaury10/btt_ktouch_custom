/* Voir wifi_mock.h pour le contrat. Implémentation minimale, sans matériel :
 * trois réseaux factices figés, un enregistrement du dernier
 * wifi_reconfigurer() demandé, et deux petits états interrogeables. AUCUN SSID
 * ni mot de passe réel — que du factice. */
#include "wifi_mock.h"

#include <stdio.h>
#include <string.h>

/* Trois réseaux factices (les mêmes que le brief) : deux chiffrés, un ouvert,
 * triés par RSSI décroissant comme le ferait wifi_scanner() réel. */
static const wifi_reseau_t g_factices[] = {
    { "MaBox_5G", -45, true },
    { "Livebox", -62, true },
    { "CafeOuvert", -70, false },
};

static char                 g_dernier_ssid[33];
static char                 g_dernier_pass[65];
static int                  g_appels_reconfigurer;
static wifi_reconfig_etat_t g_reconfig_etat = WIFI_RECONFIG_INACTIF;
static char                 g_etat_ssid[33] = "MaBox_5G";
static bool                 g_etat_connecte = true;

esp_err_t wifi_scanner(wifi_reseau_t *sortie, size_t max, size_t *nb)
{
    if (sortie == NULL || nb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t total = sizeof(g_factices) / sizeof(g_factices[0]);
    if (total > max) {
        total = max;
    }
    for (size_t i = 0; i < total; i++) {
        sortie[i] = g_factices[i];
    }
    *nb = total;
    return ESP_OK;
}

esp_err_t wifi_reconfigurer(const char *ssid, const char *pass)
{
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_reconfig_etat == WIFI_RECONFIG_EN_COURS) {
        return ESP_ERR_INVALID_STATE; /* même contrat de non-réentrance que wifi.c */
    }
    snprintf(g_dernier_ssid, sizeof(g_dernier_ssid), "%s", ssid);
    snprintf(g_dernier_pass, sizeof(g_dernier_pass), "%s", pass != NULL ? pass : "");
    g_appels_reconfigurer++;
    g_reconfig_etat = WIFI_RECONFIG_EN_COURS;
    return ESP_OK;
}

void wifi_etat(char *ssid_sortie, size_t taille, bool *connecte)
{
    if (ssid_sortie != NULL && taille > 0) {
        snprintf(ssid_sortie, taille, "%s", g_etat_ssid);
    }
    if (connecte != NULL) {
        *connecte = g_etat_connecte;
    }
}

wifi_reconfig_etat_t wifi_reconfig_etat(void)
{
    return g_reconfig_etat;
}

/* --- Crochets d'introspection (host-test) ------------------------------- */

void wifi_mock_reset(void)
{
    g_dernier_ssid[0] = '\0';
    g_dernier_pass[0] = '\0';
    g_appels_reconfigurer = 0;
    g_reconfig_etat = WIFI_RECONFIG_INACTIF;
    snprintf(g_etat_ssid, sizeof(g_etat_ssid), "%s", "MaBox_5G");
    g_etat_connecte = true;
}

const char *wifi_mock_dernier_ssid(void) { return g_dernier_ssid; }
const char *wifi_mock_dernier_pass(void) { return g_dernier_pass; }
int         wifi_mock_appels_reconfigurer(void) { return g_appels_reconfigurer; }

void wifi_mock_definir_reconfig_etat(wifi_reconfig_etat_t etat)
{
    g_reconfig_etat = etat;
}

void wifi_mock_definir_etat(const char *ssid, bool connecte)
{
    snprintf(g_etat_ssid, sizeof(g_etat_ssid), "%s", ssid != NULL ? ssid : "");
    g_etat_connecte = connecte;
}
