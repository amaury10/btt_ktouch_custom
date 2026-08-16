/* Fix round 1 (revue tache 1, jalon 3a, MAJOR) : web_nb_macros_serialisables()
 * est la fonction pure extraite de web.c qui borne le nombre de macros
 * serialisees par /state a KLIPPER_MACROS_MAX -- voir web_macros.h pour le
 * pourquoi (nb_macros amont n'est pas garanti borne, macros_tronquees existe
 * justement pour signaler le cas ou il ne l'est pas). web.c lui-meme
 * (esp_http_server.h) ne se compile pas au harnais hote ; cette fonction,
 * elle, est deliberement independante d'ESP-IDF pour rester testable ici. */
#include "etat_klipper.h"
#include "petit_test.h"
#include "web_macros.h"

void suite_web_macros(void)
{
    printf("suite : web (macros)\n");

    VERIFIER(web_nb_macros_serialisables(0) == 0);
    VERIFIER(web_nb_macros_serialisables(1) == 1);
    VERIFIER(web_nb_macros_serialisables(KLIPPER_MACROS_MAX - 1) == KLIPPER_MACROS_MAX - 1);
    VERIFIER(web_nb_macros_serialisables(KLIPPER_MACROS_MAX) == KLIPPER_MACROS_MAX);

    /* Le cas qui declenchait le depassement avant ce fix : un producteur
     * amont annonce plus de macros que la place allouee. */
    VERIFIER(web_nb_macros_serialisables(KLIPPER_MACROS_MAX + 1) == KLIPPER_MACROS_MAX);
    VERIFIER(web_nb_macros_serialisables(200) == KLIPPER_MACROS_MAX);

    /* Borne haute du type : uint8_t ne depasse jamais 255, mais le clamp
     * doit tenir meme la, sans dependre du fait que 255 < 256 tombe pile
     * sur une puissance de deux. */
    VERIFIER(web_nb_macros_serialisables(255) == KLIPPER_MACROS_MAX);
}
