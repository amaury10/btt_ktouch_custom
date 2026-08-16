#include "web_macros.h"

#include "etat_klipper.h"

uint8_t web_nb_macros_serialisables(uint8_t nb_macros)
{
    return nb_macros < KLIPPER_MACROS_MAX ? nb_macros : (uint8_t)KLIPPER_MACROS_MAX;
}
