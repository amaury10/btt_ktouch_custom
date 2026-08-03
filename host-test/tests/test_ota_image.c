#include "petit_test.h"
#include "ota_image.h"

#include <string.h>

void suite_ota_image(void)
{
    printf("suite : ota_image\n");

    /* --- ota_image_entete_valide --- */
    uint8_t magic_ok[1] = {0xE9};
    uint8_t magic_ko[1] = {0x00};

    /* magic + taille dans les bornes : accepte */
    VERIFIER(ota_image_entete_valide(magic_ok, 1, 1000, 100, 2000) == true);
    /* magic invalide : rejete */
    VERIFIER(ota_image_entete_valide(magic_ko, 1, 1000, 100, 2000) == false);
    /* debut NULL : rejete */
    VERIFIER(ota_image_entete_valide(NULL, 1, 1000, 100, 2000) == false);
    /* n == 0 : rejete */
    VERIFIER(ota_image_entete_valide(magic_ok, 0, 1000, 100, 2000) == false);
    /* taille_image en-dessous de taille_min : rejete */
    VERIFIER(ota_image_entete_valide(magic_ok, 1, 99, 100, 2000) == false);
    /* taille_image au-dessus de taille_partition : rejete */
    VERIFIER(ota_image_entete_valide(magic_ok, 1, 2001, 100, 2000) == false);
    /* bornes exactes acceptees : taille_min et taille_partition inclus */
    VERIFIER(ota_image_entete_valide(magic_ok, 1, 100, 100, 2000) == true);
    VERIFIER(ota_image_entete_valide(magic_ok, 1, 2000, 100, 2000) == true);

    /* --- ota_backup_entete_serialiser / parser : aller-retour --- */
    ota_backup_entete_t e;
    e.magic = OTA_BACKUP_MAGIC;
    e.taille = 0x01020304u;
    for (int i = 0; i < 32; i++) {
        e.sha256[i] = (uint8_t)(i + 1);
    }

    uint8_t tampon[OTA_BACKUP_ENTETE_TAILLE];
    VERIFIER(ota_backup_entete_serialiser(&e, tampon, sizeof(tampon)) == true);

    /* Verification explicite du little-endian octet par octet (magic et
     * taille), independamment du boutisme natif de la machine hote. */
    VERIFIER(tampon[0] == (uint8_t)(OTA_BACKUP_MAGIC & 0xFF));
    VERIFIER(tampon[1] == (uint8_t)((OTA_BACKUP_MAGIC >> 8) & 0xFF));
    VERIFIER(tampon[2] == (uint8_t)((OTA_BACKUP_MAGIC >> 16) & 0xFF));
    VERIFIER(tampon[3] == (uint8_t)((OTA_BACKUP_MAGIC >> 24) & 0xFF));
    VERIFIER(tampon[4] == 0x04);
    VERIFIER(tampon[5] == 0x03);
    VERIFIER(tampon[6] == 0x02);
    VERIFIER(tampon[7] == 0x01);

    ota_backup_entete_t relu;
    memset(&relu, 0xAA, sizeof(relu));
    VERIFIER(ota_backup_entete_parser(tampon, sizeof(tampon), &relu) == true);
    VERIFIER(relu.magic == e.magic);
    VERIFIER(relu.taille == e.taille);
    VERIFIER(memcmp(relu.sha256, e.sha256, 32) == 0);

    /* serialiser : sortie NULL ou tampon trop court -> false */
    VERIFIER(ota_backup_entete_serialiser(&e, NULL, OTA_BACKUP_ENTETE_TAILLE) == false);
    VERIFIER(ota_backup_entete_serialiser(&e, tampon, OTA_BACKUP_ENTETE_TAILLE - 1) == false);
    /* serialiser : entete NULL -> false */
    VERIFIER(ota_backup_entete_serialiser(NULL, tampon, sizeof(tampon)) == false);

    /* parser : src NULL -> false */
    VERIFIER(ota_backup_entete_parser(NULL, sizeof(tampon), &relu) == false);
    /* parser : tampon trop court -> false */
    VERIFIER(ota_backup_entete_parser(tampon, OTA_BACKUP_ENTETE_TAILLE - 1, &relu) == false);
    /* parser : sortie NULL -> false */
    VERIFIER(ota_backup_entete_parser(tampon, sizeof(tampon), NULL) == false);
    /* parser : magic errone -> false */
    uint8_t tampon_mauvais_magic[OTA_BACKUP_ENTETE_TAILLE];
    memcpy(tampon_mauvais_magic, tampon, sizeof(tampon));
    tampon_mauvais_magic[0] ^= 0xFF; /* casse le magic */
    VERIFIER(ota_backup_entete_parser(tampon_mauvais_magic, sizeof(tampon_mauvais_magic), &relu) == false);

    /* --- ota_sha256_egal --- */
    uint8_t sha_a[32];
    uint8_t sha_b[32];
    for (int i = 0; i < 32; i++) {
        sha_a[i] = (uint8_t)(i * 3);
        sha_b[i] = (uint8_t)(i * 3);
    }
    VERIFIER(ota_sha256_egal(sha_a, sha_b) == true);

    /* un seul octet different, a chaque position tour a tour : rejete */
    sha_b[0] ^= 0x01;
    VERIFIER(ota_sha256_egal(sha_a, sha_b) == false);
    sha_b[0] = sha_a[0];
    sha_b[31] ^= 0x01;
    VERIFIER(ota_sha256_egal(sha_a, sha_b) == false);
}
