/* Implementation : voir backend_jouet.h pour le contrat. */
#include "backend_jouet.h"

#include <stdio.h>
#include <string.h>

#include "journal.h"

static const char *TAG = "backend_jouet";

/* Contrat de core/backend.h : `etat` pointe TOUJOURS vers un tampon remis a
 * zero par le socle juste avant l'appel de rafraichir() -- ce backend porte
 * donc son propre compteur d'un cycle a l'autre dans une variable statique de
 * fichier, jamais en le relisant depuis `etat` (meme technique que
 * g_progression_scenario1 dans firmware/main/core/backend_factice.c). */
static uint32_t g_compteur = 0;

static esp_err_t backend_jouet_demarrer(void *etat, const backend_hote_t *hote)
{
    (void)hote;
    (void)etat;
    /* N'ecrit rien dans `etat` : il arrive deja a zero (contrat backend.h),
     * et le tampon "avant" (etat_store_init()) l'est aussi -- laisser les
     * deux identiques ici, comme backend_factice_demarrer(), evite de faire
     * avancer generation avant le tout premier rafraichir() reel (RED
     * observe en ecrivant test_jouet.c : une premiere validation qui ecrit
     * ne serait-ce que `libelle` ferait sauter generation a 1 au lieu de
     * rester a 0, exactement le piege que ce commentaire existe pour
     * eviter). */
    g_compteur = 0;
    JOURNAL_INFO(TAG, "demarrage");
    return ESP_OK;
}

static esp_err_t backend_jouet_rafraichir(void *etat)
{
    etat_jouet_t *e = (etat_jouet_t *)etat;
    g_compteur++;
    e->compteur = g_compteur;
    snprintf(e->libelle, sizeof(e->libelle), "toy");
    return ESP_OK;
}

static void backend_jouet_arreter(void *etat)
{
    (void)etat;
    JOURNAL_INFO(TAG, "arret");
}

static esp_err_t backend_jouet_commande(void *etat, const char *action, const char *arguments_json)
{
    (void)etat;
    (void)arguments_json;

    if (strcmp(action, "reset") == 0) {
        g_compteur = 0;
        JOURNAL_INFO(TAG, "commande reset");
        return ESP_OK;
    }

    /* Action inconnue : echec fort et explicite (meme politique que
     * backend_factice_commande()), jamais un succes silencieux. */
    JOURNAL_ALERTE(TAG, "commande inconnue %s", action);
    return ESP_ERR_NOT_SUPPORTED;
}

static const backend_desc_t g_backend_jouet_desc = {
    .nom = "jouet",
    .taille_etat = sizeof(etat_jouet_t),
    .demarrer = backend_jouet_demarrer,
    .rafraichir = backend_jouet_rafraichir,
    .arreter = backend_jouet_arreter,
    .commande = backend_jouet_commande,
};

const backend_desc_t *backend_jouet_desc(void)
{
    return &g_backend_jouet_desc;
}
