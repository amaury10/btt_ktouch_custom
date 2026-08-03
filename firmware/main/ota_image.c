#include "ota_image.h"

bool ota_image_entete_valide(const uint8_t *debut, size_t n, size_t taille_image,
                             size_t taille_min, size_t taille_partition)
{
    if (debut == NULL || n == 0) {
        return false;
    }
    if (debut[0] != 0xE9) {
        return false;
    }
    if (taille_image < taille_min || taille_image > taille_partition) {
        return false;
    }
    return true;
}

bool ota_backup_entete_serialiser(const ota_backup_entete_t *e, uint8_t *sortie, size_t taille)
{
    if (e == NULL || sortie == NULL || taille < OTA_BACKUP_ENTETE_TAILLE) {
        return false;
    }

    /* Little-endian explicite, octet par octet -- identique hote/ESP quel
     * que soit le boutisme natif du compilateur. */
    sortie[0] = (uint8_t)(e->magic & 0xFFu);
    sortie[1] = (uint8_t)((e->magic >> 8) & 0xFFu);
    sortie[2] = (uint8_t)((e->magic >> 16) & 0xFFu);
    sortie[3] = (uint8_t)((e->magic >> 24) & 0xFFu);

    sortie[4] = (uint8_t)(e->taille & 0xFFu);
    sortie[5] = (uint8_t)((e->taille >> 8) & 0xFFu);
    sortie[6] = (uint8_t)((e->taille >> 16) & 0xFFu);
    sortie[7] = (uint8_t)((e->taille >> 24) & 0xFFu);

    for (size_t i = 0; i < 32; i++) {
        sortie[8 + i] = e->sha256[i];
    }

    return true;
}

bool ota_backup_entete_parser(const uint8_t *src, size_t taille, ota_backup_entete_t *sortie)
{
    if (src == NULL || sortie == NULL || taille < OTA_BACKUP_ENTETE_TAILLE) {
        return false;
    }

    uint32_t magic = (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
                      ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
    if (magic != OTA_BACKUP_MAGIC) {
        return false;
    }

    uint32_t taille_image = (uint32_t)src[4] | ((uint32_t)src[5] << 8) |
                             ((uint32_t)src[6] << 16) | ((uint32_t)src[7] << 24);

    sortie->magic = magic;
    sortie->taille = taille_image;
    for (size_t i = 0; i < 32; i++) {
        sortie->sha256[i] = src[8 + i];
    }

    return true;
}

bool ota_sha256_egal(const uint8_t a[32], const uint8_t b[32])
{
    uint8_t diff = 0;
    for (size_t i = 0; i < 32; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}
