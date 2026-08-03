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

    /* --- ota_taille_alignee (tache 3, extrait de ota.c) --- */
    /* deja alignee : inchangee (0 est un multiple de tout secteur). */
    VERIFIER(ota_taille_alignee(0, 4096) == 0);
    VERIFIER(ota_taille_alignee(4096, 4096) == 4096);
    VERIFIER(ota_taille_alignee(8192, 4096) == 8192);
    /* non alignee : arrondie au multiple SUPERIEUR. */
    VERIFIER(ota_taille_alignee(1, 4096) == 4096);
    VERIFIER(ota_taille_alignee(4095, 4096) == 4096);
    VERIFIER(ota_taille_alignee(4097, 4096) == 8192);
    /* valeur realiste : en-tete (40) + image app0 pleine (0x480000). */
    VERIFIER(ota_taille_alignee(0x480028u, 4096) == 0x481000u);
    /* secteur nul : 0 (evite une division par zero cote appelant). */
    VERIFIER(ota_taille_alignee(1234, 0) == 0);
    /* secteur non-puissance-de-2 : arrondi au multiple superieur quand meme
       (l'implementation utilise %, pas un masque de bits -- reste correcte
       meme si le flux ESP n'utilise en pratique que des secteurs de 4096). */
    VERIFIER(ota_taille_alignee(10, 3) == 12);
    VERIFIER(ota_taille_alignee(9, 3) == 9);

    /* --- ota_hex_vers_sha256 (tache 4, dry-run /ota) --- */
    uint8_t sha_attendu[32];
    for (int i = 0; i < 32; i++) {
        sha_attendu[i] = (uint8_t)(i * 7 + 1);
    }
    char hex_correct[65] = {0};
    for (int i = 0; i < 32; i++) {
        snprintf(hex_correct + i * 2, 3, "%02x", sha_attendu[i]);
    }

    uint8_t sortie[32];
    memset(sortie, 0xAA, sizeof(sortie));
    VERIFIER(ota_hex_vers_sha256(hex_correct, sortie) == true);
    VERIFIER(memcmp(sortie, sha_attendu, 32) == 0);

    /* majuscules acceptees, meme resultat */
    char hex_majuscules[65];
    for (int i = 0; hex_correct[i] != '\0'; i++) {
        char c = hex_correct[i];
        hex_majuscules[i] = (c >= 'a' && c <= 'f') ? (char)(c - 'a' + 'A') : c;
    }
    hex_majuscules[64] = '\0';
    memset(sortie, 0xAA, sizeof(sortie));
    VERIFIER(ota_hex_vers_sha256(hex_majuscules, sortie) == true);
    VERIFIER(memcmp(sortie, sha_attendu, 32) == 0);

    /* hex NULL -> false */
    VERIFIER(ota_hex_vers_sha256(NULL, sortie) == false);
    /* sortie NULL -> false */
    VERIFIER(ota_hex_vers_sha256(hex_correct, NULL) == false);
    /* trop court -> false */
    VERIFIER(ota_hex_vers_sha256("abcd", sortie) == false);
    /* trop long -> false */
    char hex_trop_long[66];
    memcpy(hex_trop_long, hex_correct, 64);
    hex_trop_long[64] = '0';
    hex_trop_long[65] = '\0';
    VERIFIER(ota_hex_vers_sha256(hex_trop_long, sortie) == false);
    /* caractere invalide en fin de chaine (63e position) : `sortie` ne doit
       pas etre modifie -- verifie qu'aucune ecriture partielle n'a eu lieu. */
    char hex_invalide_fin[65];
    memcpy(hex_invalide_fin, hex_correct, 65);
    hex_invalide_fin[63] = 'g'; /* hors alphabet hexa */
    memset(sortie, 0xAA, sizeof(sortie));
    VERIFIER(ota_hex_vers_sha256(hex_invalide_fin, sortie) == false);
    for (int i = 0; i < 32; i++) {
        VERIFIER(sortie[i] == 0xAA);
    }
    /* caractere invalide au debut -> false */
    char hex_invalide_debut[65];
    memcpy(hex_invalide_debut, hex_correct, 65);
    hex_invalide_debut[0] = 'z';
    VERIFIER(ota_hex_vers_sha256(hex_invalide_debut, sortie) == false);
    /* chaine vide -> false */
    VERIFIER(ota_hex_vers_sha256("", sortie) == false);
}
