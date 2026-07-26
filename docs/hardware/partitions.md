# Partitionnement de la flash K-Touch

## Vue d'ensemble

La K-Touch dispose d'une flash de 16 Mio (0x1000000 octets) partitionnée selon le schéma suivant, identique à celui du firmware officiel BIGTREETECH v1.1.0 (`K-Touch_v1.1.0_partition.bin`).

## Table de partitions

| Partition | Type | Sous-type | Offset | Taille | Fins |
|-----------|------|-----------|--------|--------|------|
| nvs | data | nvs | 0x9000 | 0x5000 (20 Kio) | Stockage clé-valeur NVS |
| otadata | data | ota | 0xE000 | 0x2000 (8 Kio) | Métadonnées OTA |
| app0 | app | ota_0 | 0x10000 | 0x480000 (4608 Kio) | Slot OTA 0 (firmware actif ou passif) |
| app1 | app | ota_1 | 0x490000 | 0x480000 (4608 Kio) | Slot OTA 1 (firmware actif ou passif) |
| spiffs | data | spiffs | 0x910000 | 0x6E0000 (7104 Kio) | Système de fichiers |
| coredump | data | coredump | 0xFF0000 | 0x10000 (64 Kio) | Vidage de cœur (crash) |

## Mécanisme OTA (Over-The-Air)

La K-Touch supporte deux slots d'application (`app0` et `app1`), permettant une mise à jour sans interruption de service. Les métadonnées de sélection sont stockées dans la partition `otadata`.

### Structure de l'otadata

La partition `otadata` (8 Kio à l'adresse 0xE000) contient deux copies identiques de 32 octets chacune :

- **Octets 0-3** : `ota_seq` — nombre de séquence (incrémenté à chaque mise à jour)
- **Octets 4-23** : `seq_label` — étiquette de séquence (20 octets, généralement 0xFF)
- **Octets 24-27** : `ota_state` — état du slot
  - `0x00000002` (VALID) : slot valide et peut être bootable
  - `0x00000003` (INVALID) : slot invalide, à ignorer
  - `0x00000004` (ABORTED) : mise à jour avortée, à ignorer
  - `0xFFFFFFFF` (UNDEFINED) : non initialisé
- **Octets 28-31** : CRC-32-LE calculé **uniquement sur les 4 octets d'`ota_seq`**

Le slot actif est déterminé par la formule : **slot = (ota_seq - 1) % 2**

Lorsqu'une mise à jour OTA arrive :
1. Le nouvel application est écrit dans le slot inactif avec `ota_state = 0x00000001` (PENDING_VERIFY)
2. Un nouvel `ota_seq` (le plus élevé des deux copies + 1) est écrit dans `otadata`
3. À la prochaine validation du boot, le bootloader accepte le nouveau slot et met à jour `ota_state` à VALID
4. Si le boot échoue avant la validation, le bootloader revient au slot précédent et marque le nouveau avec ABORTED

Chaque copie de la structure de 32 octets possède son propre CRC ; une structure avec un CRC invalide est ignorée. Si les deux copies sont invalides ou absentes, le démarrage échoue.

## Règles de sécurité critiques

Les adresses suivantes **ne doivent jamais être écrites** sans une vérification minutieuse :

- **0x0** — bootloader principal (ESP-IDF)
- **0x8000** — table de partitions
- **0x10000** — slot OTA 0 (peut être l'application active)

Toute erreur d'écriture à ces adresses rend l'appareil inutilisable.

## Vérification de la sauvegarde

Avant toute opération de programmation, une sauvegarde complète de 16 Mio est créée et vérifiée à l'aide de :

```bash
python -m ktouch dump.bin
```

Cette commande :
1. Vérifie que la taille est exactement 16 Mio
2. Valide que la table de partitions correspond exactement au schéma officiel ci-dessus
3. Rapporte le contenu des deux slots OTA (versions de firmware, dates, versions IDF)
4. Indique quel slot est actuellement actif
5. Affiche un verdict : **« Sauvegarde exploitable : OUI »** ou **« NON — ne pas reprogrammer »**

La sauvegarde ne doit être utilisée pour la restauration que si le rapport indique `safe_to_flash = True`.

## Références

- Partition officielle : `K-Touch_v1.1.0_partition.bin` du dépôt BIGTREETECH
- Documentation ESP-IDF : [OTA Updates](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html)
