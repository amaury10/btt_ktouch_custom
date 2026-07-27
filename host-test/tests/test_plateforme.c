#include "petit_test.h"
#include "plateforme.h"

void suite_plateforme(void)
{
    printf("suite : plateforme\n");
    VERIFIER(plateforme_wifi_barres(-40) == 4); /* signal excellent */
    VERIFIER(plateforme_wifi_barres(-55) == 4); /* borne haute 4 barres */
    VERIFIER(plateforme_wifi_barres(-56) == 3); /* juste sous 4 barres */
    VERIFIER(plateforme_wifi_barres(-65) == 3); /* borne 3 barres */
    VERIFIER(plateforme_wifi_barres(-75) == 2); /* borne 2 barres */
    VERIFIER(plateforme_wifi_barres(-85) == 1); /* borne 1 barre */
    VERIFIER(plateforme_wifi_barres(-90) == 0); /* signal inutilisable */
    /* Le RSSI d'un ESP32 non associé vaut 0 : ne pas le rendre comme un
     * signal parfait, sans quoi la barre d'état afficherait quatre barres
     * pleines sur un appareil hors réseau. C'est à plateforme_wifi() de ne
     * pas appeler cette conversion quand `associe` est faux et de poser
     * `barres = 0` : la fonction pure, elle, doit continuer à rendre 4 ici,
     * sans quoi la borne haute (>= -55 → 4) casserait. */
    VERIFIER(plateforme_wifi_barres(0) == 4); /* rssi 0 (non associe) ne vaut pas 4 barres */
}
