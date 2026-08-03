/* ota_image.h — helpers PURS (aucune dependance ESP, aucun acces flash) pour
 * l'OTA : validation de l'en-tete d'une image applicative recue, et
 * serialisation/parsing de l'en-tete de sauvegarde ecrite en tete du blob
 * SPIFFS avant un flash OTA. Le calcul du SHA-256 lui-meme reste cote ESP
 * (mbedtls) -- ici on ne teste que le format, pas le hash materiel. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Valide le debut d'une image applicative ESP : premier octet == 0xE9 (magic
   ESP), et taille_image dans [taille_min, taille_partition]. `debut`/`n` = les
   premiers octets recus (n>=1 requis pour lire le magic). Rend false si debut
   NULL, n==0, magic != 0xE9, ou taille hors bornes. */
bool ota_image_entete_valide(const uint8_t *debut, size_t n, size_t taille_image,
                             size_t taille_min, size_t taille_partition);

#define OTA_BACKUP_MAGIC 0x4B544241u /* "ABTK" en little-endian */
typedef struct { uint32_t magic; uint32_t taille; uint8_t sha256[32]; } ota_backup_entete_t;
#define OTA_BACKUP_ENTETE_TAILLE 40u /* 4 + 4 + 32 */

/* Serialise l'en-tete dans `sortie` (>= OTA_BACKUP_ENTETE_TAILLE) : magic et
   taille en LITTLE-ENDIAN explicite (octet par octet, pas de memcpy d'un uint32
   -> portable + testable identiquement hote/ESP), puis les 32 octets de sha256.
   Rend false si sortie NULL ou taille < OTA_BACKUP_ENTETE_TAILLE. */
bool ota_backup_entete_serialiser(const ota_backup_entete_t *e, uint8_t *sortie, size_t taille);

/* Parse l'inverse. Rend false si src NULL, taille < OTA_BACKUP_ENTETE_TAILLE,
   ou magic lu != OTA_BACKUP_MAGIC. */
bool ota_backup_entete_parser(const uint8_t *src, size_t taille, ota_backup_entete_t *sortie);

/* Compare deux SHA-256 en TEMPS CONSTANT (pas de court-circuit -- accumule le
   OU des XOR sur les 32 octets). Rend true si egaux. */
bool ota_sha256_egal(const uint8_t a[32], const uint8_t b[32]);

/* Arrondit `taille` au multiple de `taille_secteur` superieur ou egal (taille
   deja alignee -> inchangee). Pure -- extrait de ota.c (tache 3, sauvegarde
   BTT vers spiffs) pour rester testable hote : sert a dimensionner
   esp_partition_erase_range(), qui exige un effacement aligne au secteur
   flash (4096 octets sur cette cible), sans tirer esp_partition.h ici. Rend
   0 si taille_secteur vaut 0 (evite une division/modulo invalide). */
size_t ota_taille_alignee(size_t taille, size_t taille_secteur);
