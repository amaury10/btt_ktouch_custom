/* Implémentation de boite_noire.h -- voir ce header pour le POURQUOI.
 * RTC_NOINIT + témoin de validité : même mécanique éprouvée que le compteur
 * de démarrages de rescue.c (survit aux resets logiciels ET aux watchdogs,
 * pas aux coupures d'alimentation -- exactement ce qu'il faut, un POWERON
 * n'a pas de crash à documenter). Écritures sans verrou : chaque bit
 * appartient à UN seul écrivain (sa tâche), et un OR/AND-NOT 32 bits est
 * atomique sur cette architecture. */
#include "boite_noire.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"

RTC_NOINIT_ATTR static uint32_t s_drapeaux;
RTC_NOINIT_ATTR static uint32_t s_temoin;

#define TEMOIN_ATTENDU 0x424E5231u /* "BNR1" */

void boite_noire_lever(uint32_t bit)
{
    if (s_temoin != TEMOIN_ATTENDU) {
        s_temoin = TEMOIN_ATTENDU;
        s_drapeaux = 0;
    }
    s_drapeaux |= bit;
}

void boite_noire_rabattre(uint32_t bit)
{
    if (s_temoin == TEMOIN_ATTENDU) {
        s_drapeaux &= ~bit;
    }
}

uint32_t boite_noire_relever(void)
{
    if (s_temoin != TEMOIN_ATTENDU) {
        /* Première mise sous tension : la RTC contient n'importe quoi. */
        s_temoin = TEMOIN_ATTENDU;
        s_drapeaux = 0;
        return 0;
    }
    uint32_t drapeaux = s_drapeaux;
    s_drapeaux = 0;
    return drapeaux;
}
#endif
