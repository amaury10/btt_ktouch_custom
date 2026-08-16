/* Sondes du mock moonraker_ws (voir moonraker_ws_mock.c) -- réservées aux
 * tests : le code de production n'inclut JAMAIS ce header. */
#pragma once

/* Nombre d'appels à moonraker_ws_demander_bobines() depuis la dernière
 * réinitialisation. */
unsigned moonraker_ws_mock_demandes_bobines(void);

void moonraker_ws_mock_reinitialiser(void);
