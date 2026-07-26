# Jalon 1 — Preuve de vie — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Faire tourner un firmware compilé par nous sur la BTT K-Touch, dans le slot OTA `app1`, écran allumé et tactile réactif, sans jamais écraser le firmware d'origine et en prouvant le retour en arrière.

**Architecture:** Deux moitiés indépendantes. Une bibliothèque Python côté PC (`tools/ktouch/`) qui sait lire les structures ESP32 — en-têtes d'images, table de partitions, `otadata` — et qui est développée en TDD pur, sans matériel. Un projet ESP-IDF (`firmware/`) qui s'appuie sur le composant `PandaTouch_IDF` de BTT référencé en sous-module Git. Le matériel n'intervient qu'à partir de la tâche 6, une fois l'outillage validé et le firmware rendu réversible, ce qui garantit qu'on ne touche jamais à l'appareil avec du code non testé.

**Accès à l'appareil : WiFi uniquement.** Le port USB-C de la K-Touch n'est pas exploitable dans ce montage, donc `esptool` est hors jeu et **aucune sauvegarde des 16 Mo n'est possible**. Le filet de sécurité initial — dumper puis restaurer octet par octet — est remplacé par trois mécanismes embarqués dans le firmware lui-même, construits à la tâche 5 : un sauvetage automatique qui rebascule sur le firmware d'origine si le réseau ne répond pas, un retour manuel par `/revert`, et une mise à jour par `/update` pour itérer sans câble. La bibliothèque Python reste utile pour analyser les images et fabriquer une `otadata`, mais elle ne pilote plus l'appareil.

**Tech Stack:** Python 3.14 + pytest 9 (déjà installés) · ESP-IDF v5.5.5 (déjà installée) · LVGL 9 · ESP32-S3 · HTTP sur le réseau local pour tout dialogue avec l'appareil.

## Global Constraints

Ces contraintes s'appliquent à **toutes** les tâches, sans rappel.

- **ESP-IDF v5.5.5**, cible `esp32s3` — installée et vérifiée sur la machine, dans `<chemin-vers-esp-idf>`. Le BSP déclare un plancher à 5.1 et son `sdkconfig.defaults` a été généré sous 5.3.1, mais la compatibilité a été contrôlée point par point contre les sources de la 5.5.5 : le pilote RGB existe (il a seulement déménagé en `components/esp_lcd/rgb/`) et tous les champs de `esp_lcd_rgb_panel_config_t` que le BSP renseigne sont présents. La v6.0 est écartée pour ce jalon : c'est une version majeure à changements incompatibles, sortie en mars 2026, alors que le BSP n'a pas bougé depuis septembre 2025 — personne ne l'a jamais compilé contre elle.
- **Avertissement de compilation attendu, à ne pas corriger :** le BSP renseigne `psram_trans_align = 64`, champ marqué `deprecated` en 5.5.5 au profit de `dma_burst_size`. La compilation aboutit et émet un avertissement de dépréciation venant du sous-module. C'est normal. Le sous-module appartient à BTT et n'est jamais modifié ici.
- **La table de partitions stock n'est jamais modifiée.** Valeurs exactes, vérifiées dans le binaire officiel : `nvs` data/nvs `0x9000`/`0x5000` · `otadata` data/ota `0xe000`/`0x2000` · `app0` app/ota_0 `0x10000`/`0x480000` · `app1` app/ota_1 `0x490000`/`0x480000` · `spiffs` data/spiffs `0x910000`/`0x6e0000` · `coredump` data/coredump `0xff0000`/`0x10000`.
- **Le firmware d'origine n'est jamais écrasé.** Il occupe un slot OTA que nous ne touchons pas, et c'est la seule façon de revenir en arrière sans accès série. En pratique : n'écrire que dans le slot rendu par `esp_ota_get_next_update_partition(NULL)`, jamais à un offset calculé à la main. Les zones `0x0` (bootloader), `0x8000` (table de partitions) et le slot du firmware d'origine sont interdites en écriture.
- **Aucun identifiant WiFi n'est commité.** Ils vivent dans `sdkconfig`, déjà exclu par `.gitignore`, et sont saisis par `idf.py menuconfig`.
- **Interdiction absolue de viser un port série.** La K-Touch n'est joignable qu'en WiFi, par son adresse IP. Les ports COM présents sur cette machine appartiennent à **d'autres projets** de l'utilisateur : un ESP32 tiers a déjà été vu énuméré sur COM6. Aucune commande `esptool`, `idf.py flash`, `idf.py monitor` ou équivalente ne doit être lancée contre un port série dans ce projet — une écriture se ferait sur la mauvaise carte, et le fait qu'un ESP32 réponde ne prouve en rien que c'est le bon. `esptool` reste dans `tools/requirements.txt` uniquement pour un usage futur, si un accès série à la K-Touch redevenait possible.
- **Aucun binaire BTT n'est commité.** Ils sont sans licence explicite. `.gitignore` les exclut déjà.
- **Aucune ligne de code issue de `nomadsgalaxy/Prusa-Connect-Touch`** (licence OCL v1.1 + SWAtt, attribution héritée dans l'UI et le code).
- **`PandaTouch_IDF` est un sous-module, jamais une copie.** Le dépôt amont n'a pas de fichier `LICENSE` ; on n'en redistribue donc rien.
- **Licence du code écrit ici : MIT.**
- Le code Python n'utilise que la bibliothèque standard. `esptool` est un outil en ligne de commande, pas une dépendance importée.
- Commentaires, docstrings et messages de commit en français ; identifiants de code en anglais.

---

### Task 1: Socle du dépôt et lecture des images ESP32

**Files:**
- Create: `LICENSE`
- Create: `README.md`
- Create: `pytest.ini`
- Create: `tools/requirements.txt`
- Create: `tools/ktouch/__init__.py`
- Create: `tools/ktouch/image.py`
- Test: `tests/test_image.py`

**Interfaces:**
- Consumes: rien.
- Produces: `ktouch.image.parse_image_header(data: bytes) -> ImageHeader`, `parse_app_desc(data: bytes) -> AppDesc`, `parse_partition_table(data: bytes) -> list[Partition]`, l'exception `NotAnEspImage`, et les dataclasses gelées `ImageHeader(chip: str, segment_count: int, entry_addr: int, flash_size: str)`, `AppDesc(project_name: str, version: str, date: str, time: str, idf_ver: str)`, `Partition(name: str, type: str, subtype: str, offset: int, size: int)`.

- [ ] **Step 1: Créer le fichier de licence et le README**

`LICENSE` — texte MIT standard, titulaire `amaury10 and contributors`, année `2026`.

`README.md` :

```markdown
# BTT K-Touch Custom

Firmware ouvert et outillage de rétro-ingénierie pour la **BIGTREETECH K-Touch**,
un écran tactile ESP32-S3 de 5 pouces dont le développement a été arrêté par son
fabricant (dernière version publiée : `v1.1.0`, novembre 2024, qui s'identifie
elle-même comme une beta).

Le projet poursuit deux buts sur une base technique commune : redonner un
firmware vivant et compilable aux possesseurs de l'appareil, et détourner
celui-ci pour piloter un tracker astrophotographique.

## État

Jalon 1 en cours : preuve de vie d'un firmware maison dans le slot OTA `app1`.
Rien n'est encore utilisable au quotidien.

## Avertissement

Reprogrammer l'appareil se fait à vos risques. Cela dit, la démarche est conçue
pour être réversible : le firmware d'origine reste intact dans le slot `app0`, et
les outils de ce dépôt vérifient chaque sauvegarde avant toute écriture.

## Licence

MIT pour le code de ce dépôt. Le support matériel provient du composant
[`bigtreetech/PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF),
référencé en sous-module et non redistribué ici.
```

`pytest.ini` :

```ini
[pytest]
pythonpath = tools
testpaths = tests
```

`tools/requirements.txt` :

```
esptool>=4.7
```

`tools/ktouch/__init__.py` : fichier vide.

- [ ] **Step 2: Écrire les tests qui échouent**

`tests/test_image.py` :

```python
"""Tests de lecture des structures d'images ESP32.

Les fixtures sont fabriquées octet par octet plutôt que lues depuis un binaire
BTT : le dépôt ne redistribue aucun binaire du fabricant, et une fixture
synthétique documente le format bien mieux qu'un blob opaque.
"""

import struct

import pytest

from ktouch.image import (
    AppDesc,
    ImageHeader,
    NotAnEspImage,
    Partition,
    parse_app_desc,
    parse_image_header,
    parse_partition_table,
)


def make_image(chip_id=0x0009, segment_count=5, entry_addr=0x4037915C, spi_size=4):
    """En-tête d'image ESP32 de 24 octets, suivi d'un en-tête de segment."""
    header = struct.pack(
        "<BBBBIB3sHBHH4sB",
        0xE9, segment_count, 3, spi_size << 4, entry_addr,
        0, b"\x00\x00\x00", chip_id, 0, 0, 0, b"\x00" * 4, 0,
    )
    segment = struct.pack("<II", 0x3C000020, 0x100)
    return header + segment


def make_app_desc(project=b"K-Touch", version=b"50747de", date=b"Nov 11 2024",
                  time=b"16:53:17", idf=b"v5.1.1-dirty"):
    """Descripteur d'application de 256 octets, placé juste après le segment."""
    pad = lambda raw, size: raw.ljust(size, b"\x00")
    return (
        struct.pack("<IIII", 0xABCD5432, 0, 0, 0)
        + pad(version, 32) + pad(project, 32)
        + pad(time, 16) + pad(date, 16) + pad(idf, 32)
        + b"\x00" * 32 + b"\x00" * 80
    )


def make_partition(name, ptype, subtype, offset, size):
    return struct.pack(
        "<HBBII16sI", 0x50AA, ptype, subtype, offset, size,
        name.encode().ljust(16, b"\x00"), 0,
    )


STOCK_TABLE = (
    make_partition("nvs", 1, 0x02, 0x9000, 0x5000)
    + make_partition("otadata", 1, 0x00, 0xE000, 0x2000)
    + make_partition("app0", 0, 0x10, 0x10000, 0x480000)
    + make_partition("app1", 0, 0x11, 0x490000, 0x480000)
    + make_partition("spiffs", 1, 0x82, 0x910000, 0x6E0000)
    + make_partition("coredump", 1, 0x03, 0xFF0000, 0x10000)
    + b"\xeb\xeb" + b"\xff" * 14 + b"\x00" * 16
)


def test_parse_image_header_reconnait_un_esp32s3():
    header = parse_image_header(make_image())
    assert header == ImageHeader(
        chip="ESP32-S3", segment_count=5, entry_addr=0x4037915C, flash_size="16MB"
    )


def test_parse_image_header_rejette_un_octet_magique_invalide():
    with pytest.raises(NotAnEspImage):
        parse_image_header(b"\x00" * 32)


def test_parse_image_header_rejette_un_buffer_trop_court():
    with pytest.raises(NotAnEspImage):
        parse_image_header(b"\xe9\x05")


def test_parse_app_desc_lit_l_identite_du_firmware():
    desc = parse_app_desc(make_image() + make_app_desc())
    assert desc == AppDesc(
        project_name="K-Touch", version="50747de",
        date="Nov 11 2024", time="16:53:17", idf_ver="v5.1.1-dirty",
    )


def test_parse_app_desc_rejette_une_image_sans_descripteur():
    with pytest.raises(NotAnEspImage):
        parse_app_desc(make_image() + b"\x00" * 256)


def test_parse_partition_table_lit_les_six_partitions_stock():
    parts = parse_partition_table(STOCK_TABLE)
    assert parts == [
        Partition("nvs", "data", "nvs", 0x9000, 0x5000),
        Partition("otadata", "data", "ota", 0xE000, 0x2000),
        Partition("app0", "app", "ota_0", 0x10000, 0x480000),
        Partition("app1", "app", "ota_1", 0x490000, 0x480000),
        Partition("spiffs", "data", "spiffs", 0x910000, 0x6E0000),
        Partition("coredump", "data", "coredump", 0xFF0000, 0x10000),
    ]


def test_parse_partition_table_s_arrete_sur_l_entree_md5():
    """L'entrée MD5 finale ne doit pas être confondue avec une partition."""
    assert len(parse_partition_table(STOCK_TABLE)) == 6


def test_parse_partition_table_accepte_une_table_vide():
    assert parse_partition_table(b"\xff" * 32) == []
```

- [ ] **Step 3: Lancer les tests pour vérifier qu'ils échouent**

Run: `python -m pytest tests/test_image.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'ktouch.image'`

- [ ] **Step 4: Écrire l'implémentation minimale**

`tools/ktouch/image.py` :

```python
"""Lecture des structures d'images ESP32 : en-tête d'image, descripteur
d'application et table de partitions.

Bibliothèque standard uniquement, pour que l'outillage reste utilisable sans
avoir installé ESP-IDF.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

IMAGE_MAGIC = 0xE9
APP_DESC_MAGIC = 0xABCD5432
PARTITION_MAGIC = 0x50AA
PARTITION_MD5_MAGIC = 0xEBEB
PARTITION_ENTRY_SIZE = 32

# 24 octets d'en-tête d'image, puis 8 octets d'en-tête du premier segment.
APP_DESC_OFFSET = 32

CHIP_IDS = {
    0x0000: "ESP32", 0x0002: "ESP32-S2", 0x0005: "ESP32-C3",
    0x0009: "ESP32-S3", 0x000C: "ESP32-C2", 0x000D: "ESP32-C6",
}
FLASH_SIZES = {0: "1MB", 1: "2MB", 2: "4MB", 3: "8MB", 4: "16MB", 5: "32MB"}
PART_TYPES = {0: "app", 1: "data"}
PART_SUBTYPES = {
    (0, 0x00): "factory", (0, 0x10): "ota_0", (0, 0x11): "ota_1",
    (1, 0x00): "ota", (1, 0x01): "phy", (1, 0x02): "nvs",
    (1, 0x03): "coredump", (1, 0x04): "nvs_keys",
    (1, 0x81): "fat", (1, 0x82): "spiffs",
}


class NotAnEspImage(ValueError):
    """Les données fournies ne sont pas une image ESP32 exploitable."""


@dataclass(frozen=True)
class ImageHeader:
    chip: str
    segment_count: int
    entry_addr: int
    flash_size: str


@dataclass(frozen=True)
class AppDesc:
    project_name: str
    version: str
    date: str
    time: str
    idf_ver: str


@dataclass(frozen=True)
class Partition:
    name: str
    type: str
    subtype: str
    offset: int
    size: int


def _cstr(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("utf-8", "replace")


def parse_image_header(data: bytes) -> ImageHeader:
    if len(data) < 24:
        raise NotAnEspImage(f"{len(data)} octets, il en faut au moins 24")
    if data[0] != IMAGE_MAGIC:
        raise NotAnEspImage(f"octet magique 0x{data[0]:02x} au lieu de 0xe9")
    entry_addr = struct.unpack_from("<I", data, 4)[0]
    chip_id = struct.unpack_from("<H", data, 12)[0]
    return ImageHeader(
        chip=CHIP_IDS.get(chip_id, f"inconnu(0x{chip_id:04x})"),
        segment_count=data[1],
        entry_addr=entry_addr,
        flash_size=FLASH_SIZES.get(data[3] >> 4, f"inconnue({data[3] >> 4})"),
    )


def parse_app_desc(data: bytes) -> AppDesc:
    base = APP_DESC_OFFSET
    if len(data) < base + 144:
        raise NotAnEspImage("image trop courte pour contenir un descripteur")
    if struct.unpack_from("<I", data, base)[0] != APP_DESC_MAGIC:
        raise NotAnEspImage("descripteur d'application absent")
    return AppDesc(
        version=_cstr(data[base + 16:base + 48]),
        project_name=_cstr(data[base + 48:base + 80]),
        time=_cstr(data[base + 80:base + 96]),
        date=_cstr(data[base + 96:base + 112]),
        idf_ver=_cstr(data[base + 112:base + 144]),
    )


def parse_partition_table(data: bytes) -> list[Partition]:
    """Lit les entrées jusqu'à l'entrée MD5 finale, une entrée vide, ou la fin."""
    partitions: list[Partition] = []
    for offset in range(0, len(data), PARTITION_ENTRY_SIZE):
        entry = data[offset:offset + PARTITION_ENTRY_SIZE]
        if len(entry) < PARTITION_ENTRY_SIZE:
            break
        magic = struct.unpack_from("<H", entry, 0)[0]
        if magic != PARTITION_MAGIC:
            break  # entrée MD5, remplissage 0xFF, ou fin de table
        _, ptype, subtype, part_offset, size, label, _flags = struct.unpack(
            "<HBBII16sI", entry
        )
        partitions.append(Partition(
            name=_cstr(label),
            type=PART_TYPES.get(ptype, str(ptype)),
            subtype=PART_SUBTYPES.get((ptype, subtype), f"0x{subtype:02x}"),
            offset=part_offset,
            size=size,
        ))
    return partitions
```

- [ ] **Step 5: Lancer les tests pour vérifier qu'ils passent**

Run: `python -m pytest tests/test_image.py -v`
Expected: PASS — 8 tests.

- [ ] **Step 6: Commit**

```bash
git add LICENSE README.md pytest.ini tools/ tests/
git commit -m "feat(tools): lecture des structures d'images ESP32"
```

---

### Task 2: Lecture et fabrication de la partition otadata

**Files:**
- Create: `tools/ktouch/otadata.py`
- Test: `tests/test_otadata.py`

**Interfaces:**
- Consumes: rien de la tâche 1.
- Produces: `ktouch.otadata.esp_crc32_le(init: int, data: bytes) -> int`, `seq_crc(ota_seq: int) -> int`, `parse_otadata(data: bytes) -> list[OtaEntry]` (lève `ValueError` sur un tampon tronqué), `active_slot(data: bytes, ota_slots: int = 2) -> int | None`, `build_otadata(slot: int, ota_slots: int = 2) -> bytes` (rend exactement 8192 octets), la dataclasse gelée `OtaEntry(ota_seq: int, ota_state: int, crc: int)` avec la propriété `valid: bool`, et les constantes `SECTOR_SIZE = 0x1000`, `OTADATA_SIZE = 0x2000`, `ENTRY_SIZE = 32`, plus les états `ESP_OTA_IMG_NEW`, `ESP_OTA_IMG_PENDING_VERIFY`, `ESP_OTA_IMG_VALID`, `ESP_OTA_IMG_INVALID`, `ESP_OTA_IMG_ABORTED`, `ESP_OTA_IMG_UNDEFINED`.

C'est la tâche la plus délicate du jalon : une `otadata` mal formée empêche l'appareil de démarrer. D'où deux garde-fous. Le CRC est vérifié contre une implémentation bit-à-bit indépendante, et non contre lui-même. Et la tâche 5 confrontera cette implémentation à l'`otadata` réelle lue sur l'appareil avant toute écriture.

**Le piège de convention, à ne pas retraverser.** `esp_rom_crc32_le` inverse le registre CRC *avant et après* le traitement, comme l'indique son en-tête `esp_rom_crc.h` : « These helpers invert the CRC register before and after processing each call. » Il est donc **identique à `zlib.crc32(data, init)`**, sans aucune compensation. Une première rédaction de ce plan appliquait une compensation `^ 0xFFFFFFFF` de part et d'autre, ce qui rend le registre brut au lieu du CRC — et la référence bit-à-bit censée la valider avait été écrite sans les inversions, donc elle confirmait l'erreur au lieu de la détecter. La référence de test ci-dessous inverse bien le registre en entrée et en sortie ; c'est ce qui la rend réellement indépendante. Contrôle de sanité disponible à tout moment : `zlib.crc32(b"123456789")` doit valoir `0xCBF43926`, la valeur de contrôle standard du CRC-32.

- [ ] **Step 1: Écrire les tests qui échouent**

`tests/test_otadata.py` :

```python
"""Tests de la partition otadata, qui détermine le slot OTA au démarrage.

Le bootloader retient, parmi les entrées dont le CRC est correct, celle qui a le
`ota_seq` le plus élevé, puis démarre le slot `(ota_seq - 1) % nombre_de_slots`.
"""

import struct

import pytest

from ktouch.otadata import (
    ENTRY_SIZE,
    ESP_OTA_IMG_ABORTED,
    ESP_OTA_IMG_INVALID,
    OTADATA_SIZE,
    SECTOR_SIZE,
    OtaEntry,
    active_slot,
    build_otadata,
    esp_crc32_le,
    parse_otadata,
    seq_crc,
)


def reference_crc32(crc: int, data: bytes) -> int:
    """CRC-32 réfléchi bit à bit, avec inversion du registre avant et après.

    Implémentation volontairement naïve et indépendante de zlib : elle sert de
    témoin pour prouver que `esp_crc32_le` reproduit bien `esp_rom_crc32_le`.
    Les deux inversions ne sont pas décoratives — ce sont elles que documente
    `esp_rom_crc.h`, et les omettre rendrait ce témoin complice de l'erreur
    qu'il est censé détecter.
    """
    register = (~crc) & 0xFFFFFFFF
    for byte in data:
        register ^= byte
        for _ in range(8):
            register = (register >> 1) ^ (0xEDB88320 if register & 1 else 0)
    return (~register) & 0xFFFFFFFF


def make_entry(ota_seq, ota_state=0x00000002, crc=None):
    if crc is None:
        crc = seq_crc(ota_seq)
    return struct.pack("<I20sII", ota_seq, b"\xff" * 20, ota_state, crc)


def make_otadata(sector0=b"", sector1=b""):
    raw = bytearray(b"\xff" * OTADATA_SIZE)
    raw[0:len(sector0)] = sector0
    raw[SECTOR_SIZE:SECTOR_SIZE + len(sector1)] = sector1
    return bytes(raw)


@pytest.mark.parametrize("seq", [0, 1, 2, 3, 17, 0xFFFF])
def test_esp_crc32_le_reproduit_l_implementation_bit_a_bit(seq):
    payload = struct.pack("<I", seq)
    assert esp_crc32_le(0xFFFFFFFF, payload) == reference_crc32(0xFFFFFFFF, payload)


@pytest.mark.parametrize("init", [0x00000000, 0x12345678, 0xFFFFFFFF])
def test_esp_crc32_le_honore_la_valeur_initiale(init):
    """L'argument `init` doit être réellement pris en compte, pas ignoré."""
    payload = b"K-Touch"
    assert esp_crc32_le(init, payload) == reference_crc32(init, payload)


def test_esp_crc32_le_donne_la_valeur_de_controle_standard():
    """Ancrage externe : CRC-32("123456789") vaut 0xCBF43926 par définition.

    `esp_rom_crc32_le` inversant le registre en entrée, il faut lui passer
    l'inverse de la valeur initiale du CRC-32 standard, soit 0.
    """
    assert esp_crc32_le(0x00000000, b"123456789") == 0xCBF43926


def test_seq_crc_valeurs_connues():
    """Vecteurs recoupés avec l'implémentation bit à bit et la valeur de contrôle."""
    assert seq_crc(1) == 0x4743989A
    assert seq_crc(2) == 0x55F63774


def test_parse_otadata_lit_les_deux_copies():
    entries = parse_otadata(make_otadata(make_entry(1), make_entry(4)))
    assert entries[0] == OtaEntry(ota_seq=1, ota_state=2, crc=seq_crc(1))
    assert entries[1] == OtaEntry(ota_seq=4, ota_state=2, crc=seq_crc(4))


def test_parse_otadata_refuse_un_buffer_trop_court():
    """Une lecture flash tronquée doit donner un message lisible, pas struct.error."""
    with pytest.raises(ValueError):
        parse_otadata(b"\xff" * 64)


def test_entree_avec_crc_faux_est_invalide():
    entry = parse_otadata(make_otadata(make_entry(1, crc=0xDEADBEEF)))[0]
    assert entry.valid is False


def test_entree_effacee_est_invalide():
    """Une partition vierge (0xFF partout) ne désigne aucun slot."""
    assert parse_otadata(make_otadata())[0].valid is False


@pytest.mark.parametrize("state", [ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED])
def test_entree_marquee_invalide_ou_abandonnee_est_rejetee(state):
    """Le bootloader écarte ces deux états même si le CRC est bon.

    C'est l'état d'un appareil qui vient de subir un retour arrière : croire
    une telle entrée valide ferait prédire le mauvais slot.
    """
    entry = parse_otadata(make_otadata(make_entry(3, ota_state=state)))[0]
    assert entry.valid is False


def test_active_slot_ignore_une_entree_abandonnee_meme_plus_recente():
    data = make_otadata(make_entry(1), make_entry(2, ota_state=ESP_OTA_IMG_ABORTED))
    assert active_slot(data) == 0


@pytest.mark.parametrize("seq,attendu", [(1, 0), (2, 1), (3, 0), (4, 1)])
def test_active_slot_alterne_avec_la_sequence(seq, attendu):
    assert active_slot(make_otadata(make_entry(seq))) == attendu


def test_active_slot_retient_la_sequence_la_plus_elevee():
    assert active_slot(make_otadata(make_entry(1), make_entry(2))) == 1


def test_active_slot_ignore_une_entree_au_crc_faux_meme_plus_recente():
    data = make_otadata(make_entry(1), make_entry(2, crc=0xDEADBEEF))
    assert active_slot(data) == 0


def test_active_slot_sans_entree_valide_rend_none():
    assert active_slot(make_otadata()) is None


@pytest.mark.parametrize("slot", [0, 1])
def test_build_otadata_est_relu_par_active_slot(slot):
    """Aller-retour : ce qu'on écrit est bien ce que le bootloader lira."""
    assert active_slot(build_otadata(slot)) == slot


def test_build_otadata_a_la_taille_exacte_de_la_partition():
    assert len(build_otadata(1)) == OTADATA_SIZE == 0x2000


def test_build_otadata_ne_laisse_qu_une_entree_valide():
    entries = parse_otadata(build_otadata(1))
    assert [e.valid for e in entries] == [True, False]


def test_build_otadata_efface_reellement_tout_le_reste():
    """Le second secteur doit être vierge, pas seulement porteur d'un CRC faux.

    C'est ce qui empêche une ancienne séquence plus élevée de l'emporter.
    """
    assert build_otadata(1)[ENTRY_SIZE:] == b"\xff" * (OTADATA_SIZE - ENTRY_SIZE)


@pytest.mark.parametrize("slot", [-1, 2, 99])
def test_build_otadata_refuse_un_slot_hors_bornes(slot):
    with pytest.raises(ValueError):
        build_otadata(slot)
```

- [ ] **Step 2: Lancer les tests pour vérifier qu'ils échouent**

Run: `python -m pytest tests/test_otadata.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'ktouch.otadata'`

- [ ] **Step 3: Écrire l'implémentation minimale**

`tools/ktouch/otadata.py` :

```python
"""Lecture et fabrication de la partition `otadata`.

Cette partition de 8 Kio détermine, au démarrage, lequel des deux slots OTA le
bootloader exécute. Elle contient deux copies d'une structure de 32 octets, une
par secteur de 4 Kio :

    uint32_t ota_seq;
    uint8_t  seq_label[20];
    uint32_t ota_state;
    uint32_t crc;        /* CRC du seul champ ota_seq */

Le bootloader retient, parmi les entrées dont le CRC est correct, celle qui a le
`ota_seq` le plus élevé, et démarre le slot `(ota_seq - 1) % nombre_de_slots`.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

SECTOR_SIZE = 0x1000
OTADATA_SIZE = 0x2000
ENTRY_FORMAT = "<I20sII"
ENTRY_SIZE = struct.calcsize(ENTRY_FORMAT)  # 32
ERASED = 0xFFFFFFFF

# esp_ota_img_states_t, dans esp_flash_partitions.h. Attention : 0x1 est
# PENDING_VERIFY, pas VALID — s'y tromper déclenche un retour arrière vers
# l'autre slot au second démarrage lorsque le rollback est actif.
ESP_OTA_IMG_NEW = 0x00000000
ESP_OTA_IMG_PENDING_VERIFY = 0x00000001
ESP_OTA_IMG_VALID = 0x00000002
ESP_OTA_IMG_INVALID = 0x00000003
ESP_OTA_IMG_ABORTED = 0x00000004
ESP_OTA_IMG_UNDEFINED = 0xFFFFFFFF


def esp_crc32_le(init: int, data: bytes) -> int:
    """Reproduit `esp_rom_crc32_le`.

    Cette fonction de la ROM inverse le registre CRC avant et après traitement,
    exactement comme `zlib.crc32` : les deux sont donc identiques, et toute
    « compensation » supplémentaire donnerait le registre brut au lieu du CRC.
    Ancrage : `esp_crc32_le(0, b"123456789")` vaut `0xCBF43926`, la valeur de
    contrôle standard du CRC-32.
    """
    return zlib.crc32(data, init) & 0xFFFFFFFF


def seq_crc(ota_seq: int) -> int:
    """CRC que le bootloader attend pour une valeur de `ota_seq` donnée."""
    return esp_crc32_le(0xFFFFFFFF, struct.pack("<I", ota_seq))


@dataclass(frozen=True)
class OtaEntry:
    ota_seq: int
    ota_state: int
    crc: int

    @property
    def valid(self) -> bool:
        """Reproduit `bootloader_common_ota_select_invalid`, inversé.

        Le bootloader écarte une entrée effacée, mais aussi une entrée marquée
        INVALID ou ABORTED — l'état dans lequel se trouve un appareil qui vient
        de subir un retour arrière.
        """
        if self.ota_seq == ERASED:
            return False
        if self.ota_state in (ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED):
            return False
        return self.crc == seq_crc(self.ota_seq)


def parse_otadata(data: bytes) -> list[OtaEntry]:
    """Lit les deux copies de l'entrée, une par secteur."""
    if len(data) < SECTOR_SIZE + ENTRY_SIZE:
        raise ValueError(
            f"otadata tronquée : {len(data)} octets, il en faut {OTADATA_SIZE}"
        )
    entries = []
    for sector in (0, 1):
        ota_seq, _label, ota_state, crc = struct.unpack_from(
            ENTRY_FORMAT, data, sector * SECTOR_SIZE
        )
        entries.append(OtaEntry(ota_seq=ota_seq, ota_state=ota_state, crc=crc))
    return entries


def active_slot(data: bytes, ota_slots: int = 2) -> int | None:
    """Slot que le bootloader démarrera, ou None si aucune entrée n'est valide.

    Sans entrée valide, le bootloader se rabat sur la partition `factory` ; la
    K-Touch n'en ayant pas, il tenterait le premier slot OTA.
    """
    valid = [entry for entry in parse_otadata(data) if entry.valid]
    if not valid:
        return None
    return (max(valid, key=lambda e: e.ota_seq).ota_seq - 1) % ota_slots


def build_otadata(slot: int, ota_slots: int = 2) -> bytes:
    """Fabrique une partition otadata complète qui fait démarrer `slot`.

    La seconde copie est laissée vierge : en réécrivant les 8 Kio, on efface
    l'historique de séquence de l'appareil, et une seule entrée fait autorité.
    """
    if not 0 <= slot < ota_slots:
        raise ValueError(f"slot {slot} hors de l'intervalle [0, {ota_slots})")
    ota_seq = slot + 1  # (ota_seq - 1) % ota_slots == slot
    entry = struct.pack(
        ENTRY_FORMAT, ota_seq, b"\xff" * 20, ESP_OTA_IMG_VALID, seq_crc(ota_seq)
    )
    raw = bytearray(b"\xff" * OTADATA_SIZE)
    raw[0:ENTRY_SIZE] = entry
    return bytes(raw)
```

- [ ] **Step 4: Lancer les tests pour vérifier qu'ils passent**

Run: `python -m pytest tests/test_otadata.py -v`
Expected: PASS, aucun échec, sortie sans avertissement (les cas paramétrés comptent séparément).

- [ ] **Step 5: Commit**

```bash
git add tools/ktouch/otadata.py tests/test_otadata.py
git commit -m "feat(tools): lecture et fabrication de la partition otadata"
```

---

### Task 3: Vérification d'une sauvegarde de flash et interface en ligne de commande

**Files:**
- Create: `tools/ktouch/dump.py`
- Create: `tools/ktouch/cli.py`
- Create: `tools/ktouch/__main__.py` (enveloppe de trois lignes autour de `cli.main`)
- Create: `ktouch-cli.py` (lanceur à la racine du dépôt — le tiret est délibéré, voir plus bas)
- Create: `docs/hardware/partitions.md`
- Test: `tests/test_dump.py`

**Interfaces:**
- Consumes: `ktouch.image.{parse_app_desc, parse_partition_table, Partition, NotAnEspImage}` et `ktouch.otadata.active_slot` des tâches 1 et 2.
- Produces: `ktouch.dump.STOCK_PARTITIONS: list[Partition]`, `ktouch.dump.FLASH_SIZE = 0x1000000`, `ktouch.dump.inspect_dump(data: bytes) -> DumpReport`, et la dataclasse gelée `DumpReport(size_ok: bool, partitions: list[Partition], partitions_match_stock: bool, app0: AppDesc | None, app1: AppDesc | None, active_slot: int | None)` avec la propriété `safe_to_flash: bool` (vraie seulement si la taille est bonne, le partitionnement identique au stock **et** `app0` lisible) et la méthode `format() -> str`. Plus le lanceur `ktouch-cli.py` à la racine, qui expose trois sous-commandes utilisables depuis le dépôt sans manipuler `PYTHONPATH` : `verify <sauvegarde.bin>`, `otadata <fichier.bin>` et `make-otadata <slot> <sortie.bin>`.

C'est le filet de sécurité : aucune écriture sur l'appareil n'aura lieu tant que ce rapport ne confirme pas que la sauvegarde est complète et conforme.

- [ ] **Step 1: Écrire les tests qui échouent**

`tests/test_dump.py` :

```python
"""Tests de la vérification d'une sauvegarde complète du flash (16 Mio)."""

import struct

import pytest

from ktouch.dump import FLASH_SIZE, STOCK_PARTITIONS, inspect_dump
from ktouch.otadata import build_otadata

from test_image import STOCK_TABLE, make_app_desc, make_image


def make_dump(table=STOCK_TABLE, slot=0, app0=True, app1=False, size=FLASH_SIZE):
    """Fabrique une sauvegarde synthétique de 16 Mio."""
    raw = bytearray(b"\xff" * size)
    raw[0x8000:0x8000 + len(table)] = table
    otadata = build_otadata(slot)
    raw[0xE000:0xE000 + len(otadata)] = otadata
    if app0:
        blob = make_image() + make_app_desc(project=b"K-Touch")
        raw[0x10000:0x10000 + len(blob)] = blob
    if app1:
        blob = make_image() + make_app_desc(project=b"ktouch-custom")
        raw[0x490000:0x490000 + len(blob)] = blob
    return bytes(raw)


def test_sauvegarde_conforme_est_declaree_sure():
    report = inspect_dump(make_dump())
    assert report.size_ok is True
    assert report.partitions_match_stock is True
    assert report.safe_to_flash is True


def test_taille_incorrecte_est_rejetee():
    report = inspect_dump(make_dump(size=0x800000))
    assert report.size_ok is False
    assert report.safe_to_flash is False


def test_app0_illisible_rend_la_sauvegarde_inexploitable():
    """Une K-Touch d'origine a toujours un app0 : son absence trahit une lecture
    corrompue, même si la taille et le partitionnement semblent corrects."""
    report = inspect_dump(make_dump(app0=False))
    assert report.size_ok is True
    assert report.partitions_match_stock is True
    assert report.app0 is None
    assert report.safe_to_flash is False


def test_table_de_partitions_differente_est_rejetee():
    """Un appareil au partitionnement inattendu ne doit pas être reprogrammé."""
    truncated = STOCK_TABLE[:32 * 3] + b"\xeb\xeb" + b"\xff" * 14 + b"\x00" * 16
    report = inspect_dump(make_dump(table=truncated))
    assert report.partitions_match_stock is False
    assert report.safe_to_flash is False


def test_les_partitions_stock_sont_celles_du_binaire_officiel():
    assert [(p.name, p.offset, p.size) for p in STOCK_PARTITIONS] == [
        ("nvs", 0x9000, 0x5000),
        ("otadata", 0xE000, 0x2000),
        ("app0", 0x10000, 0x480000),
        ("app1", 0x490000, 0x480000),
        ("spiffs", 0x910000, 0x6E0000),
        ("coredump", 0xFF0000, 0x10000),
    ]


def test_identifie_le_firmware_present_dans_app0():
    report = inspect_dump(make_dump())
    assert report.app0 is not None
    assert report.app0.project_name == "K-Touch"


def test_slot_vide_est_rapporte_comme_absent():
    report = inspect_dump(make_dump(app1=False))
    assert report.app1 is None


def test_lit_le_slot_actif_dans_otadata():
    assert inspect_dump(make_dump(slot=1, app1=True)).active_slot == 1


def test_le_rapport_texte_mentionne_les_deux_slots():
    rendu = inspect_dump(make_dump(app1=True)).format()
    assert "app0" in rendu and "app1" in rendu
    assert "K-Touch" in rendu and "ktouch-custom" in rendu
```

`tests/test_cli.py` — les sous-commandes sont ce que l'opérateur tape devant un
appareil branché, donc leurs codes de sortie et leur robustesse aux mauvais
arguments comptent autant que la logique qu'elles appellent :

```python
"""Tests des sous-commandes en ligne de commande."""

from pathlib import Path

import pytest

from ktouch.cli import main
from ktouch.otadata import OTADATA_SIZE, active_slot

from test_dump import make_dump


@pytest.fixture
def sauvegarde(tmp_path: Path) -> Path:
    chemin = tmp_path / "dump.bin"
    chemin.write_bytes(make_dump())
    return chemin


@pytest.mark.parametrize("argv", [
    ["ktouch-cli.py"],
    ["ktouch-cli.py", "inconnue"],
    ["ktouch-cli.py", "verify"],
    ["ktouch-cli.py", "verify", "a", "b"],
    ["ktouch-cli.py", "make-otadata", "1"],
])
def test_arguments_invalides_rendent_2(argv):
    assert main(argv) == 2


def test_fichier_absent_rend_2_sans_trace_d_appel(tmp_path):
    assert main(["ktouch-cli.py", "verify", str(tmp_path / "absent.bin")]) == 2


def test_verify_rend_0_sur_une_sauvegarde_conforme(sauvegarde):
    assert main(["ktouch-cli.py", "verify", str(sauvegarde)]) == 0


def test_verify_rend_1_sur_une_sauvegarde_tronquee(tmp_path):
    chemin = tmp_path / "court.bin"
    chemin.write_bytes(make_dump()[:0x800000])
    assert main(["ktouch-cli.py", "verify", str(chemin)]) == 1


def test_otadata_accepte_une_sauvegarde_complete(sauvegarde, capsys):
    assert main(["ktouch-cli.py", "otadata", str(sauvegarde)]) == 0
    assert "slot actif : app0" in capsys.readouterr().out


def test_make_otadata_produit_un_fichier_relisible(tmp_path):
    sortie = tmp_path / "otadata.bin"
    assert main(["ktouch-cli.py", "make-otadata", "1", str(sortie)]) == 0
    contenu = sortie.read_bytes()
    assert len(contenu) == OTADATA_SIZE
    assert active_slot(contenu) == 1


@pytest.mark.parametrize("slot", ["2", "-1", "abc"])
def test_make_otadata_refuse_un_slot_invalide(slot, tmp_path):
    assert main(["ktouch-cli.py", "make-otadata", slot, str(tmp_path / "x.bin")]) == 2


def test_image_identifie_un_binaire_applicatif(tmp_path, capsys):
    from test_image import make_app_desc, make_image

    chemin = tmp_path / "app.bin"
    chemin.write_bytes(make_image() + make_app_desc(project=b"ktouch-custom"))
    assert main(["ktouch-cli.py", "image", str(chemin)]) == 0
    sortie = capsys.readouterr().out
    assert "ESP32-S3" in sortie and "ktouch-custom" in sortie


def test_image_rejette_un_fichier_quelconque(tmp_path):
    chemin = tmp_path / "bidon.bin"
    chemin.write_bytes(b"\x00" * 4096)
    assert main(["ktouch-cli.py", "image", str(chemin)]) == 1


def test_le_lanceur_racine_ne_masque_pas_le_paquet():
    """Régression : un `ktouch.py` à la racine masquait `tools/ktouch/`.

    Le nom du lanceur doit rester non importable — un tiret garantit qu'aucun
    `import ktouch` ne puisse le trouver à la place du paquet.
    """
    racine = Path(__file__).resolve().parent.parent
    assert not (racine / "ktouch.py").exists()
    assert (racine / "ktouch-cli.py").exists()
```

- [ ] **Step 2: Lancer les tests pour vérifier qu'ils échouent**

Run: `python -m pytest tests/test_dump.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'ktouch.dump'`

- [ ] **Step 3: Écrire l'implémentation minimale**

`tools/ktouch/dump.py` :

```python
"""Vérification d'une sauvegarde complète du flash de la K-Touch.

Contrôle qu'une sauvegarde fait bien 16 Mio, que sa table de partitions est
exactement celle du firmware d'origine, et rapporte ce que contient chaque slot
OTA. Aucune écriture sur l'appareil ne doit avoir lieu sans un rapport
`safe_to_flash`.
"""

from __future__ import annotations

from dataclasses import dataclass

from ktouch.image import (
    AppDesc,
    NotAnEspImage,
    Partition,
    parse_app_desc,
    parse_partition_table,
)
from ktouch.otadata import active_slot as read_active_slot

FLASH_SIZE = 0x1000000  # 16 Mio
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0x1000

# Relevé dans K-Touch_v1.1.0_partition.bin, publié par BIGTREETECH.
STOCK_PARTITIONS = [
    Partition("nvs", "data", "nvs", 0x9000, 0x5000),
    Partition("otadata", "data", "ota", 0xE000, 0x2000),
    Partition("app0", "app", "ota_0", 0x10000, 0x480000),
    Partition("app1", "app", "ota_1", 0x490000, 0x480000),
    Partition("spiffs", "data", "spiffs", 0x910000, 0x6E0000),
    Partition("coredump", "data", "coredump", 0xFF0000, 0x10000),
]

OTADATA_OFFSET = 0xE000
OTADATA_SIZE = 0x2000


@dataclass(frozen=True)
class DumpReport:
    size_ok: bool
    partitions: list[Partition]
    partitions_match_stock: bool
    app0: AppDesc | None
    app1: AppDesc | None
    active_slot: int | None

    @property
    def safe_to_flash(self) -> bool:
        """Vrai si la sauvegarde permet une restauration intégrale.

        Exiger un `app0` lisible n'est pas de la coquetterie : une K-Touch
        d'origine en contient toujours un. S'il est illisible alors que la
        table de partitions est intacte, c'est le signe d'une lecture
        corrompue — précisément la sauvegarde sur laquelle il ne faut pas
        compter comme filet de secours.
        """
        return self.size_ok and self.partitions_match_stock and self.app0 is not None

    def format(self) -> str:
        lines = [
            f"Taille          : {'16 Mio, conforme' if self.size_ok else 'INCORRECTE'}",
            f"Partitionnement : {'identique au stock' if self.partitions_match_stock else 'DIFFERENT DU STOCK'}",
            f"Slot actif      : {'aucun' if self.active_slot is None else f'app{self.active_slot}'}",
        ]
        for index, desc in ((0, self.app0), (1, self.app1)):
            if desc is None:
                lines.append(f"  app{index}          : vide")
            else:
                lines.append(
                    f"  app{index}          : {desc.project_name} "
                    f"({desc.version}, {desc.date}, IDF {desc.idf_ver})"
                )
        verdict = "OUI" if self.safe_to_flash else "NON — ne pas reprogrammer"
        lines.append(f"Sauvegarde exploitable : {verdict}")
        return "\n".join(lines)


def _app_desc_at(data: bytes, offset: int) -> AppDesc | None:
    try:
        return parse_app_desc(data[offset:offset + 0x1000])
    except NotAnEspImage:
        return None


def inspect_dump(data: bytes) -> DumpReport:
    table = data[PARTITION_TABLE_OFFSET:PARTITION_TABLE_OFFSET + PARTITION_TABLE_SIZE]
    partitions = parse_partition_table(table)
    otadata = data[OTADATA_OFFSET:OTADATA_OFFSET + OTADATA_SIZE]
    slot = read_active_slot(otadata) if len(otadata) == OTADATA_SIZE else None
    return DumpReport(
        size_ok=len(data) == FLASH_SIZE,
        partitions=partitions,
        partitions_match_stock=partitions == STOCK_PARTITIONS,
        app0=_app_desc_at(data, 0x10000),
        app1=_app_desc_at(data, 0x490000),
        active_slot=slot,
    )
```

`tools/ktouch/cli.py` :

```python
"""Interface en ligne de commande de l'outillage K-Touch.

Trois sous-commandes, qui couvrent tout ce dont les étapes sur matériel ont
besoin. Elles existent pour qu'aucune manipulation d'`otadata` ne passe par un
`python -c` tapé à la main : c'est l'opération qui, mal faite, empêche
l'appareil de démarrer, et une ligne recopiée de travers au mauvais moment est
un risque réel.
"""

import sys

from ktouch.dump import FLASH_SIZE, OTADATA_OFFSET, inspect_dump
from ktouch.image import NotAnEspImage, parse_app_desc, parse_image_header
from ktouch.otadata import OTADATA_SIZE, active_slot, build_otadata, parse_otadata, seq_crc

APP_SLOT_SIZE = 0x480000  # taille d'un slot OTA de la K-Touch

USAGE = """usage :
  python ktouch-cli.py verify <sauvegarde.bin>        vérifie une sauvegarde de 16 Mio
  python ktouch-cli.py otadata <fichier.bin>          détaille l'otadata (blob de 8 Kio
                                                      ou sauvegarde complète)
  python ktouch-cli.py make-otadata <slot> <sortie>   fabrique une otadata démarrant <slot>
  python ktouch-cli.py image <firmware.bin>           identifie une image applicative
"""


def _read(chemin: str) -> bytes | None:
    try:
        with open(chemin, "rb") as handle:
            return handle.read()
    except OSError as erreur:
        print(f"lecture impossible : {erreur}", file=sys.stderr)
        return None


def _cmd_verify(chemin: str) -> int:
    data = _read(chemin)
    if data is None:
        return 2
    report = inspect_dump(data)
    print(report.format())
    return 0 if report.safe_to_flash else 1


def _cmd_otadata(chemin: str) -> int:
    """Accepte indifféremment un blob d'otadata ou une sauvegarde complète."""
    data = _read(chemin)
    if data is None:
        return 2
    if len(data) == FLASH_SIZE:
        data = data[OTADATA_OFFSET:OTADATA_OFFSET + OTADATA_SIZE]
        print(f"sauvegarde complète : otadata extraite à 0x{OTADATA_OFFSET:x}")
    if len(data) < OTADATA_SIZE:
        print(f"taille inattendue : {len(data)} octets", file=sys.stderr)
        return 2
    for index, entree in enumerate(parse_otadata(data)):
        attendu = seq_crc(entree.ota_seq)
        print(
            f"  copie {index} : ota_seq={entree.ota_seq} "
            f"state=0x{entree.ota_state:08x} crc=0x{entree.crc:08x} "
            f"attendu=0x{attendu:08x} valide={entree.valid}"
        )
    slot = active_slot(data)
    print(f"slot actif : {'aucun' if slot is None else f'app{slot}'}")
    return 0


def _cmd_make_otadata(slot_texte: str, sortie: str) -> int:
    try:
        slot = int(slot_texte)
    except ValueError:
        print(f"slot invalide : {slot_texte}", file=sys.stderr)
        return 2
    try:
        contenu = build_otadata(slot)
    except ValueError as erreur:
        print(str(erreur), file=sys.stderr)
        return 2
    try:
        with open(sortie, "wb") as handle:
            handle.write(contenu)
    except OSError as erreur:
        print(f"écriture impossible : {erreur}", file=sys.stderr)
        return 2
    print(f"{len(contenu)} octets écrits dans {sortie} — démarrera sur app{slot}")
    return 0


def _cmd_image(chemin: str) -> int:
    """Identifie une image applicative — sert à valider un binaire fraîchement
    compilé avec les mêmes analyseurs que ceux qui liront le vrai matériel."""
    data = _read(chemin)
    if data is None:
        return 2
    try:
        entete = parse_image_header(data)
        desc = parse_app_desc(data)
    except NotAnEspImage as erreur:
        print(f"pas une image ESP32 exploitable : {erreur}", file=sys.stderr)
        return 1
    print(f"puce        : {entete.chip}")
    print(f"flash       : {entete.flash_size}, {entete.segment_count} segments")
    print(f"projet      : {desc.project_name}")
    print(f"version     : {desc.version}")
    print(f"compilation : {desc.date} {desc.time}, IDF {desc.idf_ver}")
    marge = APP_SLOT_SIZE - len(data)
    print(f"taille      : {len(data)} octets sur {APP_SLOT_SIZE} ({marge} libres)")
    return 0 if marge >= 0 else 1


def main(argv: list[str]) -> int:
    if len(argv) >= 2:
        commande, arguments = argv[1], argv[2:]
        if commande == "verify" and len(arguments) == 1:
            return _cmd_verify(arguments[0])
        if commande == "otadata" and len(arguments) == 1:
            return _cmd_otadata(arguments[0])
        if commande == "make-otadata" and len(arguments) == 2:
            return _cmd_make_otadata(arguments[0], arguments[1])
        if commande == "image" and len(arguments) == 1:
            return _cmd_image(arguments[0])
    print(USAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
```

`tools/ktouch/__main__.py`, pour que `python -m ktouch` reste utilisable depuis
`tools/` :

```python
"""Permet `python -m ktouch <sous-commande>` depuis le dossier `tools`."""

import sys

from ktouch.cli import main

raise SystemExit(main([__spec__.name.split(".")[0], *sys.argv[1:]]))
```

`ktouch-cli.py`, à la racine du dépôt. Le paquet vit sous `tools/` pour ne pas
encombrer la racine, ce qui le rend invisible à `python -m ktouch` — le
`pythonpath` de `pytest.ini` ne s'applique qu'à pytest. Ce lanceur ajoute
`tools/` au chemin de recherche.

**Le tiret dans le nom est délibéré et load-bearing.** Un fichier `ktouch.py` à
la racine masquerait le paquet `tools/ktouch/` dès qu'un interpréteur est lancé
depuis le dépôt : `import ktouch` trouverait le module racine, pas le paquet, et
toute commande d'inspection échouerait sur `'ktouch' is not a package`. Un nom
comportant un tiret n'est pas un identifiant Python valide, donc il ne peut pas
être importé — le conflit devient structurellement impossible.

```python
"""Lanceur : rend le paquet `ktouch` accessible depuis la racine du dépôt."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "tools"))

from ktouch.cli import main  # noqa: E402  (après ajustement du chemin)

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
```

- [ ] **Step 4: Lancer les tests pour vérifier qu'ils passent**

Run: `python -m pytest tests/ -v`
Expected: PASS — les tests des trois tâches, aucun échec.

- [ ] **Step 5: Écrire la documentation du partitionnement**

`docs/hardware/partitions.md` — reprend le tableau des six partitions avec offsets et tailles, explique le mécanisme des deux slots OTA et de l'`otadata`, et énonce la règle de sécurité : ne jamais écrire à `0x0`, `0x8000` ni `0x10000`. Indique que ces valeurs proviennent de `K-Touch_v1.1.0_partition.bin` du dépôt officiel BTT, et comment les revérifier avec `python ktouch-cli.py verify <sauvegarde.bin>`.

Ce document sert à relire un vidage hexadécimal à la main : ses offsets doivent donc être exacts. Points à énoncer sans approximation.

L'`otadata` contient **deux copies indépendantes** de 32 octets, une au début de chaque secteur de 4 Kio — donc aux adresses absolues `0xE000` et `0xF000`, et non aux octets 0-31 et 32-63. Elles ne sont normalement **pas** identiques : c'est justement la différence d'`ota_seq` qui porte le mécanisme de sélection.

La disposition d'une entrée est : octets 0-3 `ota_seq` (uint32 LE), octets 4-23 `seq_label` (20 octets), octets 24-27 `ota_state` (uint32 LE), octets 28-31 `crc` (uint32 LE). Le CRC ne couvre **que** les 4 octets d'`ota_seq`, pas l'entrée entière. Les valeurs d'`ota_state` qu'on rencontre en pratique sont `0x2` VALID, `0x3` INVALID, `0x4` ABORTED et `0xFFFFFFFF` UNDEFINED — en gardant à l'esprit que `0x1` est PENDING_VERIFY et non VALID.

Le bootloader retient l'entrée valide au plus grand `ota_seq` et démarre le slot `(ota_seq - 1) % 2`. **Si aucune entrée n'est valide, le démarrage n'échoue pas** : le bootloader se rabat sur la partition `factory`, et à défaut sur le premier slot OTA — donc sur le firmware d'origine en `app0`. Il faut le dire explicitement, parce que c'est le chemin de secours du projet : effacer l'`otadata` ramène au stock, ça ne brique pas l'appareil.

- [ ] **Step 6: Commit**

```bash
git add tools/ktouch/dump.py tools/ktouch/cli.py tools/ktouch/__main__.py ktouch-cli.py tests/ docs/hardware/partitions.md
git commit -m "feat(tools): verification des sauvegardes de flash et CLI"
```

---

### Task 4: Projet ESP-IDF minimal qui compile

**Files:**
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/partitions.csv`
- Create: `firmware/sdkconfig.defaults`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/idf_component.yml`
- Create: `firmware/main/app_main.c`
- Create: `firmware/README.md`
- Create: `.gitmodules` (via `git submodule add`)
- Modify: `.gitignore` — retirer `*.bin` de l'exclusion globale n'est **pas** souhaitable ; vérifier simplement que `firmware/build/` est bien couvert par la règle `build/`.

**Interfaces:**
- Consumes: `ktouch.image.parse_app_desc` et `parse_image_header` de la tâche 1, pour vérifier le binaire produit.
- Produces: `firmware/build/ktouch-custom.bin`, une image ESP32-S3 de moins de 4 718 592 octets (`0x480000`).

Aucun matériel n'est nécessaire pour cette tâche : elle s'arrête à la compilation.

- [ ] **Step 1: Activer ESP-IDF v5.5.5**

**Déjà fait.** ESP-IDF v5.5.5 est installée et fonctionnelle, avec la cible `esp32s3`, la chaîne Xtensa, CMake et Ninja. Il n'y a rien à télécharger.

Il n'existe pas de raccourci « ESP-IDF PowerShell » dans le menu Démarrer sur cette machine ; l'environnement s'active en sourçant le script d'export, ce qui doit être fait **dans chaque session PowerShell** avant toute commande `idf.py` :

```powershell
& "<chemin-vers-esp-idf>\export.ps1"
```

Expected: `Done! You can now compile ESP-IDF projects.`

Vérification de la version réelle du framework — noter que `idf.py --version` renvoie sur Windows la version du lanceur `idf-exe` (`v1.0.3`) et non celle d'ESP-IDF, ce qui prête à confusion :

```powershell
git -C "<chemin-vers-esp-idf>" describe --tags
```

Expected: `v5.5.5`.

> Le framework réside sous `Downloads`. Ça fonctionne, mais c'est un emplacement que les outils de nettoyage et de sauvegarde traitent parfois à part : si la chaîne disparaît un jour sans raison apparente, c'est la première piste. Le déplacer se fait en relançant `install.ps1` depuis le nouvel emplacement.

- [ ] **Step 2: Ajouter le BSP en sous-module**

```bash
git submodule add https://github.com/bigtreetech/PandaTouch_IDF.git firmware/components/PandaTouch_IDF
git submodule update --init --recursive
```

Vérification : `firmware/components/PandaTouch_IDF/include/pandatouch_display.h` existe, et `git status` montre `.gitmodules` plus une entrée de sous-module — **aucun fichier source de BTT ne doit apparaître comme ajouté**. C'est le point qui garantit qu'on ne redistribue rien.

- [ ] **Step 3: Écrire les fichiers du projet**

`firmware/partitions.csv` — copie exacte de la table stock, à ne jamais reprogrammer sur l'appareil ; elle sert uniquement à ce que la compilation connaisse la taille des slots :

```csv
# Table de partitions d'origine de la BTT K-Touch, relevée dans
# K-Touch_v1.1.0_partition.bin. Elle n'est PAS reprogrammée sur l'appareil :
# elle informe la compilation de la taille maximale de l'application.
# Nom,      Type, SousType, Offset,    Taille
nvs,        data, nvs,      0x9000,    0x5000
otadata,    data, ota,      0xe000,    0x2000
app0,       app,  ota_0,    0x10000,   0x480000
app1,       app,  ota_1,    0x490000,  0x480000
spiffs,     data, spiffs,   0x910000,  0x6e0000
coredump,   data, coredump, 0xff0000,  0x10000
```

`firmware/sdkconfig.defaults` — reprend la configuration validée par le BSP (PSRAM octale, flash 16 Mio, LVGL 9) et y ajoute la table de partitions personnalisée :

```
CONFIG_IDF_TARGET="esp32s3"

CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000

CONFIG_BOOTLOADER_FLASH_DC_AWARE=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_120M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y

CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_SPIRAM_SPEED_120M=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10

CONFIG_FREERTOS_HZ=1000
CONFIG_LV_USE_CUSTOM_MALLOC=y
CONFIG_LV_COLOR_MIX_ROUND_OFS=0
CONFIG_LV_BUILD_EXAMPLES=n
CONFIG_LV_BUILD_DEMOS=n
CONFIG_IDF_EXPERIMENTAL_FEATURES=y
```

`firmware/CMakeLists.txt` :

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ktouch-custom)
```

`firmware/main/CMakeLists.txt` :

```cmake
idf_component_register(SRCS "app_main.c" INCLUDE_DIRS ".")
```

`firmware/main/idf_component.yml` :

```yaml
dependencies:
  idf: ">=5.5"
  lvgl/lvgl: "~9.2.0"
```

> Le composant `PandaTouch_IDF` n'est pas listé ici : il est présent en sous-module dans `firmware/components/`, où ESP-IDF le découvre automatiquement. Il tire lui-même `esp_lcd_touch_gt911` et `usb_host_msc` via son propre manifeste.

`firmware/main/app_main.c` — mire de test et retour tactile dans le journal :

```c
/* Preuve de vie du jalon 1 : allumer le panneau, afficher une mire lisible et
 * confirmer que le tactile remonte des coordonnées cohérentes.
 *
 * Le pinout utilisé est celui du Panda Touch 7 pouces, fourni par le BSP. Toute
 * l'expérience consiste à savoir s'il convient tel quel à la K-Touch 5 pouces. */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "pandatouch_display.h"
#include "pandatouch_lvgl_touch.h"

static const char *TAG = "preuve_de_vie";

static void on_touch(lv_event_t *event)
{
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    ESP_LOGI(TAG, "appui a x=%d y=%d", (int)point.x, (int)point.y);
}

static void build_test_pattern(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), LV_PART_MAIN);

    /* Bandes primaires : un canal de couleur inversé ou une broche de données
     * flottante se voit immédiatement. */
    static const uint32_t colours[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_create(screen);
        lv_obj_set_size(bar, 200, 80);
        lv_obj_set_pos(bar, i * 200, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(colours[i]), LV_PART_MAIN);
        lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    }

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "K-Touch custom\nslot app1 — preuve de vie");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    /* Repères de coin : valident que les 800x480 sont bien balayés en entier. */
    static const lv_align_t corners[] = {
        LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT,
        LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT,
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *marker = lv_obj_create(screen);
        lv_obj_set_size(marker, 24, 24);
        lv_obj_align(marker, corners[i], 0, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        lv_obj_set_style_border_width(marker, 0, LV_PART_MAIN);
    }

    lv_obj_add_event_cb(screen, on_touch, LV_EVENT_PRESSED, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "demarrage du firmware de preuve de vie");
    ESP_ERROR_CHECK(pt_display_init());
    ESP_ERROR_CHECK(pt_lvgl_touch_init());
    pt_backlight_set(80);

    PT_LVGL_SCOPE_LOCK() {
        build_test_pattern();
    }

    ESP_LOGI(TAG, "interface construite, le panneau doit etre allume");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "toujours vivant");
    }
}
```

> **Point de vigilance :** le nom exact de la fonction d'initialisation du tactile doit être relu dans `firmware/components/PandaTouch_IDF/include/pandatouch_lvgl_touch.h` après l'ajout du sous-module, ainsi que les exemples de `examples/`. Si la signature diffère de `pt_lvgl_touch_init()`, adapter l'appel — c'est la seule inconnue d'API de cette tâche.

`firmware/README.md` — explique comment installer ESP-IDF, initialiser le sous-module, compiler, et rappelle que l'installation sur l'appareil est décrite dans `docs/hardware/flashing.md`.

- [ ] **Step 4: Compiler**

```powershell
cd firmware
idf.py set-target esp32s3
idf.py build
```

Expected: la compilation aboutit et affiche la taille du binaire ainsi que l'espace libre dans la partition applicative.

- [ ] **Step 5: Vérifier le binaire produit avec notre propre outillage**

```powershell
python ktouch-cli.py image firmware/build/ktouch-custom.bin
```

Expected: `puce : ESP32-S3`, `flash : 16MB`, `projet : ktouch-custom`, et une taille très inférieure aux 4 718 592 octets du slot.

> Cette étape n'est pas décorative : elle prouve que la bibliothèque des tâches 1 à 3 lit correctement un binaire réel, et pas seulement les fixtures synthétiques.

- [ ] **Step 6: Commit**

```bash
git add .gitmodules firmware/
git commit -m "feat(firmware): projet ESP-IDF minimal de preuve de vie"
```

---

### Task 5: Rendre le firmware réversible sans câble série

**Files:**
- Create: `firmware/main/wifi.c`, `firmware/main/wifi.h`
- Create: `firmware/main/netlog.c`, `firmware/main/netlog.h`
- Create: `firmware/main/rescue.c`, `firmware/main/rescue.h`
- Create: `firmware/main/web.c`, `firmware/main/web.h`
- Create: `firmware/main/Kconfig.projbuild`
- Modify: `firmware/main/app_main.c`, `firmware/main/CMakeLists.txt`, `firmware/sdkconfig.defaults`
- Modify: `docs/hardware/flashing.md` (créé ici, pas à la tâche 6)

**Interfaces:**
- Consumes: le firmware compilable de la tâche 4.
- Produces: `wifi_start(void) -> esp_err_t`, `wifi_is_connected(void) -> bool`, `wifi_ip_string(char *out, size_t len) -> bool` ; `netlog_init(void) -> esp_err_t` et `netlog_snapshot(char *out, size_t len) -> size_t` ; `rescue_arm(uint32_t delai_ms) -> esp_err_t`, `rescue_disarm(void) -> void`, `rescue_switch_to_other_slot(void) -> esp_err_t`, `rescue_count_boot(void) -> uint32_t`, `rescue_reset_boot_count(void) -> void` ; `web_start(void) -> esp_err_t`.

> **Le rappel du minuteur ne doit rien faire de lourd.** `esp_timer` l'exécute dans sa propre tâche, dont la pile est étroite et qu'ESP-IDF documente comme ne devant jamais bloquer. Or basculer de slot implique une vérification SHA-256 de l'image cible, une allocation, une projection de la flash, puis un `esp_restart()` qui appelle les gestionnaires d'arrêt enregistrés — dont `esp_wifi_stop`, un appel bloquant. Le rappel doit donc se contenter de signaler une petite tâche dédiée, créée au moment de l'armement, qui fait le travail.

**Pourquoi cette tâche existe.** L'appareil n'est atteignable qu'en WiFi : son port USB-C n'est pas exploitable ici, donc `esptool` est hors jeu et **aucune sauvegarde des 16 Mo n'est possible**. Le filet de sécurité prévu à l'origine — dumper puis restaurer octet par octet — n'existe plus.

Sans lui, installer la mire LVGL de la tâche 4 serait un aller sans retour : ce firmware n'a pas de pile réseau, donc une fois démarré dessus, plus aucun moyen de reprendre la main. L'appareil afficherait des bandes de couleur jusqu'à ce qu'un accès série redevienne possible.

Le remplacement du filet, c'est le firmware lui-même. Trois mécanismes, du plus automatique au plus manuel.

**Le sauvetage automatique** est le seul qui fonctionne quand tout va mal. Au démarrage, avant même de tenter quoi que ce soit d'autre, un compte à rebours est armé. Si le WiFi n'est pas connecté à son échéance, le firmware bascule la partition de démarrage sur l'autre slot et redémarre — donc revient au firmware d'origine, tout seul, sans intervention. Il ne dépend ni du réseau, ni de l'écran, ni du tactile.

> **Un minuteur seul ne suffit pas, et c'est le défaut qui a failli passer.** Il ne couvre que les pannes plus lentes que son échéance. Or la panne la plus probable de ce jalon — un pinout de panneau qui ne convient pas — fait échouer l'initialisation de l'écran en moins d'une seconde. Avec un `ESP_ERROR_CHECK`, cela déclenche un `abort()`, donc un redémarrage immédiat, donc une boucle de redémarrage dont le minuteur de 90 secondes ne voit jamais l'échéance. Le sauvetage serait resté parfaitement correct et parfaitement inutile, précisément sur l'hypothèse que le jalon existe pour tester.
>
> Deux mesures ferment cette classe entière de pannes. **Aucune défaillance locale n'est fatale** : ni l'écran, ni le rétroéclairage, ni le tactile, ni le serveur ne sont vérifiés par `ESP_ERROR_CHECK` — on journalise et on continue. Et **un compteur de démarrages** survit aux redémarrages en mémoire `RTC_NOINIT_ATTR` : incrémenté à l'entrée d'`app_main`, remis à zéro dès qu'une adresse IP est obtenue, il bascule immédiatement sur l'autre slot au-delà de trois tentatives. Une boucle de redémarrage, quelle qu'en soit la cause — panique, chien de garde, débordement de pile — se solde donc par un retour au firmware d'origine.

**Le retour manuel** couvre le cas où le WiFi marche mais où l'affichage est raté : une requête sur `/revert` rebascule sur l'autre slot. Comme le firmware d'origine n'est jamais écrasé, revenir au stock ne demande aucun téléversement, juste un changement de slot.

**L'itération** sur le pinout ne passe **pas** par notre firmware. C'est contre-intuitif, et c'est le point que la conception initiale avait faux.

Notre firmware tourne depuis `app1`, puisque c'est le slot inactif que l'OTA du firmware d'origine choisit. Avec deux slots seulement, le « slot inactif » vu depuis `app1` est donc `app0` — celui du firmware d'origine. Un `/update` dans notre firmware ne pourrait écrire nulle part ailleurs, et `esp_ota_begin()` en `OTA_SIZE_UNKNOWN` efface la partition entière **avant** de recevoir le moindre octet. La première mise à jour effacerait donc le firmware d'origine, après quoi le sauvetage n'aurait plus rien vers quoi basculer.

> **Notre firmware n'expose donc aucune route de mise à jour.** Cette décision n'est pas de la prudence excessive : avec deux slots, il n'existe aucune façon pour lui d'écrire ailleurs que sur le firmware d'origine.

La boucle d'itération correcte repasse à chaque fois par le firmware d'origine, et ne touche jamais `app0` :

1. `/revert` sur notre firmware — retour au firmware d'origine dans `app0` ;
2. `/update` du firmware d'origine — écrit notre nouvelle version dans `app1` et démarre dessus ;
3. essai, observation ;
4. retour à l'étape 1.

> **Contrainte absolue :** ce firmware n'écrit jamais dans une partition applicative, quelle qu'elle soit. La seule écriture en flash qu'il pratique est celle d'`otadata`, huit kibioctets qui désignent le slot de démarrage.

- [ ] **Step 1: Déclarer les identifiants WiFi hors du dépôt**

`firmware/main/Kconfig.projbuild` :

```
menu "K-Touch custom"

    config KTOUCH_WIFI_SSID
        string "SSID du réseau WiFi"
        default ""
        help
            Renseigné via `idf.py menuconfig`. Stocké dans `sdkconfig`, qui est
            exclu du dépôt : les identifiants ne sont jamais commités.

    config KTOUCH_WIFI_PASSWORD
        string "Mot de passe WiFi"
        default ""

    config KTOUCH_RESCUE_TIMEOUT_MS
        int "Délai avant sauvetage automatique (ms)"
        default 90000
        help
            Si le WiFi n'est pas connecté à l'échéance, le firmware rebascule
            sur l'autre slot OTA et redémarre. C'est le seul filet qui
            fonctionne sans réseau ni écran.

endmenu
```

Renseigner ensuite le SSID et le mot de passe :

```powershell
& "<chemin-vers-esp-idf>\export.ps1"; cd "<racine-du-depot>\firmware"; idf.py menuconfig
```

Vérification : `git status` ne doit montrer **aucune** modification de fichier suivi contenant le SSID. `sdkconfig` est déjà exclu par `.gitignore`.

- [ ] **Step 2: Écrire le sauvetage automatique**

C'est la partie dont tout le reste dépend : elle doit être écrite en premier et rester la plus simple possible.

`firmware/main/rescue.c` :

```c
/* Sauvetage automatique : sans accès série, c'est le seul moyen de revenir au
 * firmware d'origine si ce firmware-ci ne parvient pas à joindre le réseau.
 *
 * Le principe est volontairement pauvre : un minuteur armé au démarrage, que
 * seule une connexion WiFi réussie désarme. Il ne dépend ni de l'écran, ni du
 * tactile, ni d'aucune bibliothèque tierce — donc il survit à leur défaillance. */

#include "rescue.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "rescue";
static esp_timer_handle_t minuteur;

esp_err_t rescue_switch_to_other_slot(void)
{
    const esp_partition_t *cible = esp_ota_get_next_update_partition(NULL);
    if (cible == NULL) {
        ESP_LOGE(TAG, "aucun autre slot OTA disponible");
        return ESP_ERR_NOT_FOUND;
    }
    /* Le slot voisin contient le firmware d'origine, que nous n'écrasons
     * jamais : la bascule suffit, il n'y a rien à téléverser. */
    esp_err_t erreur = esp_ota_set_boot_partition(cible);
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "bascule impossible : %s", esp_err_to_name(erreur));
        return erreur;
    }
    ESP_LOGW(TAG, "bascule vers %s, redemarrage", cible->label);
    return ESP_OK;
}

/* Dernier recours : effacer otadata.

   Si `esp_ota_set_boot_partition` refuse la cible — image absente, à moitié
   écrite, ou qui échoue la vérification — insister ne sert à rien. Mais une
   otadata invalide n'est pas un blocage : le bootloader se rabat alors sur
   `factory`, et faute de partition `factory` il démarre le premier slot OTA,
   c'est-à-dire `app0`. Ce chemin est tolérant là où `esp_ota_set_boot_partition`
   est intransigeant, puisque le bootloader essaie chaque slot à son tour. */
static esp_err_t effacer_otadata(void)
{
    const esp_partition_t *ota = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (ota == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGE(TAG, "dernier recours : effacement d'otadata");
    return esp_partition_erase_range(ota, 0, ota->size);
}

static void sur_echeance(void *arg)
{
    ESP_LOGE(TAG, "reseau injoignable dans le delai imparti");
    if (rescue_switch_to_other_slot() != ESP_OK) {
        effacer_otadata();
    }
    esp_restart();
}

esp_err_t rescue_arm(uint32_t delai_ms)
{
    const esp_timer_create_args_t args = {
        .callback = sur_echeance,
        .name = "rescue",
    };
    esp_err_t erreur = esp_timer_create(&args, &minuteur);
    if (erreur != ESP_OK) {
        return erreur;
    }
    ESP_LOGW(TAG, "sauvetage arme : %lu ms pour joindre le reseau", (unsigned long)delai_ms);
    return esp_timer_start_once(minuteur, (uint64_t)delai_ms * 1000);
}

void rescue_disarm(void)
{
    if (minuteur != NULL) {
        esp_timer_stop(minuteur);
        esp_timer_delete(minuteur);
        minuteur = NULL;
        ESP_LOGI(TAG, "sauvetage desarme : le reseau repond");
    }
}
```

`firmware/main/rescue.h` déclare les trois fonctions et rien d'autre.

- [ ] **Step 3: Écrire la connexion WiFi**

> ### ⚠ Le piège qui briquerait l'appareil définitivement
>
> La partition `nvs` (`0x9000`) est **partagée par les deux slots applicatifs**. Le firmware d'origine y range ses identifiants WiFi dans l'espace de noms standard d'ESP-IDF — vérifié dans son binaire, qui contient `nvs.net80211` et `sta.ssid`.
>
> Or `esp_wifi_set_config()` **persiste dans cette NVS**, puisque le stockage par défaut est `WIFI_STORAGE_FLASH`. Appeler cette fonction avec un SSID vide effacerait donc les identifiants du firmware d'origine.
>
> L'enchaînement serait sans retour : notre firmware ne se connecte pas, le sauvetage se déclenche, l'appareil rebascule sur le firmware d'origine — **qui n'a plus de WiFi**. Et comme le WiFi est le seul accès, l'appareil devient définitivement injoignable. Le sauvetage aurait parfaitement fonctionné tout en rendant la situation irrécupérable.
>
> **Deux règles en découlent, non négociables.** Appeler `esp_wifi_set_storage(WIFI_STORAGE_RAM)` juste après `esp_wifi_init()`, pour qu'aucune écriture de configuration ne puisse jamais atteindre la NVS. Et ne pas appeler `esp_wifi_set_config()` du tout dans le cas normal.

**Notre firmware n'a pas besoin d'identifiants : il hérite de ceux du firmware d'origine.** C'est la conséquence heureuse du même partage de NVS. `esp_wifi_init()` charge la configuration station déjà enregistrée ; il suffit ensuite d'appeler `esp_wifi_connect()` pour rejoindre le même réseau, sans que le SSID ni le mot de passe transitent où que ce soit.

`firmware/main/wifi.c` — station ESP-IDF, dans cet ordre : `esp_netif_init`, boucle d'événements par défaut, `esp_netif_create_default_wifi_sta`, `esp_wifi_init` avec la configuration par défaut, **puis immédiatement `esp_wifi_set_storage(WIFI_STORAGE_RAM)`**, mode `WIFI_MODE_STA`, `esp_wifi_start`, et enfin `esp_wifi_connect()`.

Lire la configuration héritée avec `esp_wifi_get_config(WIFI_IF_STA, &config)` après `esp_wifi_init()` et journaliser le SSID trouvé — c'est le premier indice à consulter si la connexion échoue. Si aucun SSID n'est enregistré, le journaliser en avertissement explicite et laisser le sauvetage faire son office.

`CONFIG_KTOUCH_WIFI_SSID` et `CONFIG_KTOUCH_WIFI_PASSWORD` ne servent que de **secours**, pour le cas où l'appareil n'aurait aucune configuration enregistrée. La règle d'application est stricte, et dans cet ordre :

1. lire la configuration héritée avec `esp_wifi_get_config(WIFI_IF_STA, &config)` ;
2. **si son SSID est non vide, s'en servir telle quelle et ignorer complètement les options Kconfig** — la NVS de l'appareil fait toujours autorité ;
3. seulement si le SSID hérité est vide, et seulement si `CONFIG_KTOUCH_WIFI_SSID` est renseigné, appliquer le secours par `esp_wifi_set_config()`.

Comme le stockage est déjà passé en `WIFI_STORAGE_RAM` à l'étape précédente, même ce chemin de secours ne peut pas écrire dans la NVS. Journaliser laquelle des deux sources a été retenue, mais **ne jamais journaliser le mot de passe** — le journal est exposé en HTTP sur `/log`.

Sur `WIFI_EVENT_STA_DISCONNECTED`, relancer `esp_wifi_connect()` — une coupure passagère ne doit pas déclencher le sauvetage tant que le délai n'est pas écoulé. Sur `IP_EVENT_STA_GOT_IP`, mémoriser l'adresse, journaliser `adresse IP : %s`, et **appeler `rescue_disarm()`** — c'est le seul endroit du firmware qui désarme le minuteur.

`wifi_is_connected()` rend l'état courant, `wifi_ip_string()` recopie l'adresse mémorisée.

- [ ] **Step 4: Écrire le journal réseau**

Sans port série, la console n'est lisible que par le réseau. Le plus simple à consulter, et le plus robuste à travers un pare-feu, est un tampon circulaire en RAM exposé en HTTP.

`firmware/main/netlog.c` — un tampon statique de 16 Kio, un mutex, et un relais installé avec `esp_log_set_vprintf()` qui écrit à la fois vers la sortie d'origine et dans le tampon. `netlog_snapshot()` recopie le contenu courant sous mutex.

> Attention au piège : le relais est appelé depuis n'importe quelle tâche, y compris pendant une interruption différée. Ne rien allouer dedans, ne pas appeler `ESP_LOG*` récursivement, et prendre le mutex avec un délai nul en abandonnant l'écriture plutôt qu'en bloquant.

- [ ] **Step 5: Écrire le serveur HTTP**

`firmware/main/web.c`, sur `esp_http_server`, quatre routes — **et aucune route de mise à jour**, pour la raison exposée plus haut :

| Route | Méthode | Rôle |
|---|---|---|
| `/` | GET | page d'état minimale en HTML, avec liens vers les autres routes |
| `/status` | GET | JSON : slot en cours, version, adresse IP, temps depuis le démarrage, mémoire libre, tactile disponible ou non, compteur de démarrages |
| `/log` | GET | texte brut, contenu de `netlog_snapshot()` |
| `/revert` | POST | `rescue_switch_to_other_slot()` puis `esp_restart()` |

Aucun code d'écriture de partition applicative ne doit figurer dans ce fichier : ni `esp_ota_begin`, ni `esp_ota_write`. La seule écriture en flash de tout le firmware est celle d'`otadata`, dans `rescue.c`.

> `/revert` est en POST délibérément. En GET, n'importe quelle requête d'un navigateur, d'un aspirateur de liens ou d'un scanner réseau redémarrerait l'appareil.

> Contrôler la valeur de retour de chaque `httpd_register_uri_handler` et journaliser un échec : une route de secours qui ne s'enregistre pas silencieusement est pire qu'une route absente.

- [ ] **Step 6: Câbler le tout dans `app_main`**

L'ordre est dicté par le sauvetage, et deux règles priment sur toute considération de lisibilité.

**Aucune défaillance locale n'est fatale.** Pas un seul `ESP_ERROR_CHECK` sur l'écran, le rétroéclairage, le tactile, le WiFi ou le serveur. `ESP_ERROR_CHECK` appelle `abort()`, donc redémarre l'appareil en moins d'une seconde — bien avant l'échéance du minuteur, qui ne se déclenche alors jamais. Sur chacun de ces étages : récupérer le code d'erreur, le journaliser, et continuer. Un écran mort doit laisser tourner le WiFi et le serveur, c'est précisément ce qui permet de diagnostiquer à distance au lieu de constater un appareil muet.

> `pt_backlight_set()` mérite une attention particulière : le BSP de BTT contient ses propres `ESP_ERROR_CHECK` sur les appels LEDC, donc un échec à l'intérieur peut abattre le processus sans que notre code y soit pour quoi que ce soit. Raison de plus pour démarrer le serveur HTTP **avant** de toucher à l'écran.

**Le serveur HTTP démarre juste après le WiFi, avant l'écran.** Sans ça, une panne d'affichage emporte simultanément le sauvetage automatique — désarmé par une connexion WiFi réussie — et la route manuelle `/revert`. Les deux voies de secours disparaîtraient d'un coup, sur la panne la plus probable du jalon.

L'ordre est donc : compteur de démarrages, sauvetage, NVS, journal réseau, WiFi, **serveur HTTP**, puis écran, mire et tactile.

Journaliser la partition d'exécution au démarrage, via `esp_ota_get_running_partition()`, avec son label et son offset : c'est la première chose à vérifier dans le journal, et elle doit annoncer `app1`.

- [ ] **Step 6 bis: Le compteur de démarrages**

Le minuteur ne couvre que les pannes plus lentes que son échéance. Le compteur ferme presque tout le reste : paniques, chien de garde, débordements de pile — tout ce qui redémarre l'appareil en moins de 90 secondes, **à condition que ce soit après le démarrage d'`app_main`**.

> **Ce que le compteur ne couvre pas, et qui a failli passer.** Une première rédaction de ce plan affirmait qu'il couvrait aussi les échecs d'initialisation de la PSRAM. C'est faux : `esp_psram_chip_init()` est appelée depuis `cpu_start.c`, **avant l'ordonnanceur et avant `app_main`**. Un échec y provoque un `abort()` alors qu'aucune ligne de notre code de secours n'a encore tourné — ni compteur, ni minuteur, ni serveur. Et comme `otadata` désigne toujours `app1`, une coupure de courant ne change rien : l'appareil recommence indéfiniment. C'est la seule classe de pannes qui rend l'appareil définitivement injoignable, et elle se traite en amont, par la configuration.

`firmware/main/rescue.c`, une variable en mémoire RTC qui survit aux redémarrages mais pas à une coupure d'alimentation :

```c
/* RTC_NOINIT_ATTR survit à un redémarrage logiciel comme à une panique, mais
 * pas à une coupure d'alimentation — exactement le comportement voulu : une
 * boucle de redémarrage est détectée, un appareil rallumé repart à zéro. */
RTC_NOINIT_ATTR static uint32_t compteur_demarrages;
RTC_NOINIT_ATTR static uint32_t temoin_validite;

#define TEMOIN_ATTENDU 0x4B544348u /* "KTCH" */
#define DEMARRAGES_MAX 3

uint32_t rescue_count_boot(void)
{
    if (temoin_validite != TEMOIN_ATTENDU) {
        /* Premier démarrage après mise sous tension : la mémoire RTC contient
         * n'importe quoi, il faut l'initialiser avant de s'y fier. */
        temoin_validite = TEMOIN_ATTENDU;
        compteur_demarrages = 0;
    }
    compteur_demarrages++;
    return compteur_demarrages;
}

void rescue_reset_boot_count(void) { compteur_demarrages = 0; }
```

Au tout début d'`app_main`, appeler `rescue_count_boot()`. Si la valeur rendue dépasse `DEMARRAGES_MAX`, ne rien tenter d'autre : **remettre le compteur à zéro**, puis basculer sur l'autre slot et redémarrer. Journaliser la valeur à chaque démarrage — c'est le premier indice à lire dans `/status`.

> **La remise à zéro sur ce chemin n'est pas une coquetterie.** `esp_restart()` est une réinitialisation logicielle : la mémoire RTC survit jusque dans le firmware d'origine, qui n'y touche jamais. Sans remise à zéro, le compteur reste à 4. À la tentative suivante, notre firmware fraîchement installé lirait 5, basculerait immédiatement, et repartirait au stock **sans jamais avoir essayé de démarrer** — et ainsi de suite à chaque essai. La boucle d'itération décrite plus haut serait cassée, et seule une coupure d'alimentation la débloquerait.

Remettre le compteur à zéro dans le gestionnaire `IP_EVENT_STA_GOT_IP`, au même endroit que `rescue_disarm()` : une connexion réussie prouve que ce firmware est viable.

- [ ] **Step 6 ter: Assagir la configuration mémoire — la seule parade au trou pré-`app_main`**

Le `sdkconfig.defaults` a été repris du BSP du Panda Touch **7 pouces**. Or toute la question de ce jalon est justement de savoir si la K-Touch 5 pouces lui ressemble. Quatre réglages y engagent la PSRAM avant même que notre code existe, et s'ils sont faux l'appareil est perdu sans recours.

`CONFIG_SPIRAM_MODE_OCT` suppose une PSRAM octale. `CONFIG_SPIRAM_SPEED_120M` la cadence à une fréquence qu'Espressif classe elle-même en expérimental — c'est la raison d'être de `CONFIG_IDF_EXPERIMENTAL_FEATURES`. Et surtout, `CONFIG_SPIRAM_IGNORE_NOTFOUND` vaut `n` par défaut : une PSRAM absente ou muette devient donc une erreur fatale dans `cpu_start.c`, pas un avertissement.

Pire encore, `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` et `CONFIG_SPIRAM_RODATA` placent le code et les constantes en PSRAM. Un timing PSRAM marginal ne se traduit alors pas par une allocation qui échoue proprement, mais par un plantage sur une lecture d'instruction — potentiellement, là encore, avant `app_main`.

Réglages retenus pour le premier vol, dans `firmware/sdkconfig.defaults` :

```
CONFIG_SPIRAM_IGNORE_NOTFOUND=y
CONFIG_SPIRAM_SPEED_80M=y
# CONFIG_SPIRAM_FETCH_INSTRUCTIONS n'est pas défini
# CONFIG_SPIRAM_RODATA n'est pas défini
```

Retirer explicitement `CONFIG_SPIRAM_SPEED_120M`, `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` et `CONFIG_SPIRAM_RODATA`. Une preuve de vie n'a besoin d'aucun des trois : le tampon d'affichage tient largement, et le firmware fait 1,1 Mo.

L'intérêt est de transformer une classe de pannes irrécupérable en une panne qui se diagnostique. Avec `IGNORE_NOTFOUND`, une PSRAM introuvable ou incompatible ne fait plus avorter le démarrage : elle fait échouer l'allocation du tampon LVGL dans le BSP, `pt_display_init()` rend une erreur, et l'on retombe exactement dans le cas déjà traité — écran mort, WiFi et `/revert` bien vivants. Autrement dit, on ramène l'inconnu matériel dans le périmètre que le reste de la conception sait gérer.

> Même raisonnement pour `CONFIG_ESPTOOLPY_FLASHFREQ_120M` : notre application tourne sous le **bootloader d'origine**, que nous ne remplaçons pas et dont nous n'avons pas vérifié qu'il accepte cette cadence. Revenir à 80 MHz pour le premier vol.

`CONFIG_SPIRAM_MEMTEST` mérite une mention à part, parce qu'il rouvre exactement le trou que le reste de ce bloc ferme. `IGNORE_NOTFOUND` ne couvre que « PSRAM introuvable », **pas « trouvée mais peu fiable »** — or c'est le mode de défaillance le plus probable d'une configuration 7 pouces sur du matériel 5 pouces. Le test mémoire vaut `y` par défaut, écrit et relit chaque octet mappé, et appelle `abort()` dans `cpu_start.c` s'il échoue : donc avant `app_main`, donc avant tout secours. Le désactiver fait apparaître le même défaut plus tard, dans une allocation qui échoue proprement, là où le firmware sait dégrader.

> ### ⚠ Le piège de `sdkconfig.defaults`, à connaître avant d'y toucher
>
> **ESP-IDF n'applique `sdkconfig.defaults` qu'aux symboles absents du `sdkconfig` déjà généré.** Il n'écrase jamais une valeur existante. Ajouter une ligne à `sdkconfig.defaults` sur un projet déjà compilé ne change donc **rien**, silencieusement — et la compilation réussit, ce qui donne toutes les apparences d'un correctif appliqué.
>
> Ce piège a mordu deux fois ici. La seconde a été la plus instructive : trois réglages de sûreté avaient été ajoutés d'un bloc, et deux d'entre eux coïncidaient par chance avec les valeurs par défaut d'ESP-IDF. Seul `CONFIG_ESP_TASK_WDT_PANIC` en différait — et c'était donc le seul à ne pas avoir pris. Autrement dit, l'unique réglage qui changeait réellement le comportement était l'unique à ne pas s'appliquer, sans aucun signe visible.
>
> **La seule manière fiable de modifier la configuration est donc de supprimer `firmware/sdkconfig` et de le régénérer** par `idf.py reconfigure`, puis de ressaisir les identifiants WiFi. Une suppression chirurgicale de quelques lignes ne suffit pas : elle laisse en place tout ce qu'on n'a pas pensé à retirer.
>
> **Et la vérification ne se fait jamais dans `sdkconfig.defaults` ni dans `sdkconfig`, mais dans `build/config/sdkconfig.h`** — le seul fichier qui reflète ce qui a réellement été compilé :
>
> ```powershell
> Select-String -Path firmware/build/config/sdkconfig.h -Pattern "CONFIG_ESP_TASK_WDT_PANIC|CONFIG_SPIRAM_MEMTEST"
> ```
>
> Attendu : `CONFIG_ESP_TASK_WDT_PANIC 1` présent, `CONFIG_SPIRAM_MEMTEST` absent.

- [ ] **Step 7: Compiler**

```powershell
& "<chemin-vers-esp-idf>\export.ps1"; cd "<racine-du-depot>\firmware"; idf.py build
```

Expected: la compilation aboutit. Le binaire grossit nettement — la pile WiFi et le serveur HTTP pèsent plus que le reste — mais reste très en deçà des 4 718 592 octets du slot.

Vérifier ensuite le binaire produit :

```powershell
python ktouch-cli.py image firmware/build/ktouch-custom.bin
```

- [ ] **Step 8: Documenter la procédure sans câble**

`docs/hardware/flashing.md` — décrit la voie WiFi de bout en bout : installation par le `/update` du firmware d'origine, retour par `/revert`, sauvetage automatique si le réseau ne répond pas, et itération par le `/update` de notre propre firmware.

Consigner explicitement ce qui a été perdu et pourquoi : sans accès série, aucune sauvegarde intégrale des 16 Mo n'est possible, donc pas de restauration octet par octet. Ce qui reste : le firmware d'origine intact dans son slot, et les images officielles publiées par BTT. Ce qui n'est pas récupérable en cas de perte : la NVS, donc les réglages et les identifiants WiFi saisis sur l'écran.

Indiquer aussi comment retrouver une voie série si le besoin s'en fait sentir — la K-Touch expose son UART via un pont sur le port USB-C, et un câble USB-C ne transportant que l'alimentation n'énumère aucun port.

- [ ] **Step 9: Commit**

```bash
git add firmware/main firmware/CMakeLists.txt firmware/sdkconfig.defaults docs/hardware/flashing.md
git commit -m "feat(firmware): wifi, journal reseau, OTA et sauvetage automatique"
```

---

### Task 6: Installation par WiFi et preuve de vie

**Files:**
- Modify: `docs/hardware/flashing.md` — consigner le déroulé réel et ce qui a été observé.

**Interfaces:**
- Consumes: `firmware/build/ktouch-custom.bin` de la tâche 5, celui qui embarque le WiFi, le journal réseau et le sauvetage.
- Produces: un appareil démarrant sur `app1` avec notre firmware — ou un diagnostic précis si le pinout du Panda Touch ne convient pas à la K-Touch.

Première tâche qui écrit sur l'appareil. Elle passe entièrement par le réseau : le mécanisme OTA du firmware d'origine se charge d'écrire dans le slot inactif et de basculer le démarrage, ce qui évite toute manipulation d'`otadata` à la main.

> **Ne jamais installer le binaire de la tâche 4.** Seul celui de la tâche 5 sait revenir en arrière. Vérifier avant tout téléversement que l'image contient bien le WiFi : `python ktouch-cli.py image firmware/build/ktouch-custom.bin` doit annoncer une taille nettement supérieure aux ~512 Ko de la tâche 4.

- [ ] **Step 1: Relever l'état de départ**

```powershell
curl.exe -s http://<IP>/update/identity
```

Expected: un objet JSON du type `{"id": "V1.0.0", "hardware": "ESP32"}`. Noter la valeur : c'est la version du firmware d'origine, et donc ce à quoi l'appareil doit revenir à la tâche 7.

> Si cette requête échoue, s'arrêter. Un appareil déjà injoignable avant toute écriture ne doit pas en recevoir une.

- [ ] **Step 2: Téléverser notre firmware par l'OTA du fabricant**

L'interface de mise à jour est servie sur `http://<IP>/update`. Le plus sûr est de passer par le navigateur et de sélectionner `firmware/build/ktouch-custom.bin`, puisque c'est le chemin que BTT a testé.

Expected: la page annonce la fin du téléversement puis le redémarrage de l'appareil.

> **Résultat à ne pas surinterpréter.** Il est possible que l'OTA d'origine refuse notre image, par exemple s'il contrôle un identifiant de produit. **Un refus est sans conséquence** : rien n'est écrit, l'appareil continue de tourner sur son firmware. Ce n'est pas un échec du jalon, c'est une information — et elle oriente vers l'autre voie, la restauration d'un accès série. Ne pas tenter de contourner le contrôle.

- [ ] **Step 3: Vérifier que notre firmware a pris la main**

Laisser une minute à l'appareil, puis :

```powershell
curl.exe -s http://<IP>/status
```

Expected: notre JSON à nous, annonçant `app1` comme partition d'exécution.

> Si rien ne répond au bout de deux minutes, **ne rien faire** : le sauvetage automatique de la tâche 5 rebascule sur l'autre slot et redémarre tout seul. Attendre, puis vérifier avec `curl.exe -s http://<IP>/update/identity` que le firmware d'origine a repris la main. C'est le mécanisme qui joue son rôle, pas une panne.

- [ ] **Step 4: Lire le journal réseau**

```powershell
curl.exe -s http://<IP>/log
```

Expected: les traces de démarrage, la partition d'exécution, l'adresse IP obtenue, puis la construction de l'interface. Si le GT911 n'a pas répondu, l'avertissement explicite ajouté à la tâche 4 doit apparaître ici — c'est ce qui distingue « tactile muet » de « rien ne marche ».

- [ ] **Step 5: Constater la preuve de vie à l'écran**

Cette étape est visuelle et ne peut pas être automatisée. Observer et consigner :

- le rétroéclairage s'allume ;
- les quatre bandes de couleur sont dans l'ordre rouge, vert, bleu, blanc, sans dominante ni canal manquant ;
- le texte est net, sans décalage horizontal ni déchirure ;
- les quatre repères jaunes sont visibles aux quatre coins, ce qui prouve que les 800×480 sont balayés ;
- l'image est stable, sans scintillement ni défilement.

> Une photographie de l'écran suffit à faire juger ces points par quelqu'un qui n'est pas devant l'appareil.

- [ ] **Step 6: Vérifier le tactile**

Appuyer successivement au centre puis sur chacun des quatre repères de coin, puis relire le journal :

```powershell
curl.exe -s http://<IP>/log
```

Expected: une ligne `appui a x=… y=…` par appui, avec des coordonnées cohérentes avec l'endroit touché — proches de `(0,0)` en haut à gauche et de `(799,479)` en bas à droite.

> Des coordonnées inversées, en miroir ou bornées trop tôt renseignent sur la configuration du GT911 et non sur le panneau : ce sont deux réglages indépendants dans le BSP.

- [ ] **Step 7: Interpréter**

Chaque symptôme désigne une zone précise, et c'est ce qui rend la tâche exploitable même en cas d'échec.

Un écran noir avec un journal réseau normal oriente vers le rétroéclairage ou la broche de reset du panneau. Une image aux couleurs fausses ou décalée oriente vers l'ordre des broches de données. Un défilement ou un scintillement oriente vers les timings et la fréquence pixel. Un affichage correct avec un tactile muet isole le GT911. Un appareil qui ne répond jamais et revient au stock par le sauvetage oriente vers le WiFi ou vers un blocage précoce, pas vers l'affichage.

- [ ] **Step 8: Documenter et commiter**

Compléter `docs/hardware/flashing.md` avec le déroulé réel, y compris les tentatives infructueuses : ce sont elles qui ont de la valeur pour quelqu'un qui refera la manipulation.

```bash
git add docs/hardware/flashing.md
git commit -m "docs(hardware): installation par WiFi et resultat de la preuve de vie"
```

---

### Task 7: Retour au firmware d'origine et clôture du jalon

**Files:**
- Create: `docs/hardware/pinout.md`
- Modify: `README.md` — mettre à jour la section « État ».
- Modify: `docs/superpowers/specs/2026-07-26-btt-ktouch-custom-design.md` — consigner le résultat du jalon.

**Interfaces:**
- Consumes: le résultat observé à la tâche 6.
- Produces: la preuve que la manipulation est réversible, et le pinout de la K-Touch documenté — confirmé ou infirmé.

Prouver la réversibilité fait partie des critères de réussite : un firmware qu'on ne peut pas désinstaller n'est pas une expérimentation, c'est un aller simple. C'est d'autant plus vrai ici qu'aucune sauvegarde intégrale n'existe.

- [ ] **Step 1: Revenir au firmware d'origine**

```powershell
curl.exe -s -X POST http://<IP>/revert
```

Expected: l'appareil accuse réception puis redémarre. Le firmware d'origine n'ayant jamais été écrasé, il n'y a rien à téléverser.

- [ ] **Step 2: Vérifier le retour**

```powershell
curl.exe -s http://<IP>/update/identity
```

Expected: à nouveau la valeur relevée à la tâche 6, étape 1 — `{"id": "V1.0.0", "hardware": "ESP32"}`. L'interface d'origine doit également être revenue à l'écran.

> Si l'appareil ne répond plus du tout, le sauvetage automatique reste la dernière ligne : il rebascule et redémarre sans intervention. S'il a lui aussi échoué, il ne reste que la voie série, décrite dans `docs/hardware/flashing.md`.

- [ ] **Step 3: Documenter le pinout**

`docs/hardware/pinout.md` — consigner le verdict, qui a de la valeur dans les deux cas puisque personne ne l'a publié.

En cas de succès : indiquer que le pinout du Panda Touch 7 pouces fonctionne tel quel sur la K-Touch 5 pouces, avec le tableau complet des broches (PCLK `GPIO5`, DE `GPIO38`, reset `GPIO46`, rétroéclairage `GPIO21`, données `17,18,48,47,39,11,12,13,14,15,16,6,7,8,9,10`, GT911 sur SCL `GPIO1` / SDA `GPIO2` / reset `GPIO41` / interruption `GPIO40`, I²C d'extension sur `GPIO3`/`GPIO4`), les timings employés, la date de la vérification et la version du firmware testée. Préciser explicitement que cela lève la réserve « may differ on a few panel GPIOs or timings » du README de Prusa-Connect-Touch.

En cas d'échec : décrire le symptôme observé, la zone qu'il désigne, et ce qui a été écarté. Ouvrir alors un jalon de rétro-ingénierie ciblé sur la fonction d'initialisation LCD du firmware d'origine, avec Ghidra et le module Xtensa — en sachant que le binaire à désassembler est celui de la V1.0.0, la version réellement présente sur l'appareil.

- [ ] **Step 4: Mettre à jour l'état du projet**

Dans `README.md`, remplacer la section « État » par le résultat réel du jalon et la suite envisagée. Dans la spec, ajouter une section « Résultat du jalon 1 » qui consigne le verdict du pinout, la réversibilité vérifiée, et le fait que tout s'est fait sans accès série.

- [ ] **Step 5: Ouvrir l'issue de licence chez BTT**

Ouvrir une issue sur `bigtreetech/PandaTouch_IDF` signalant que le README affiche un badge « License: MIT » pointant vers un fichier `LICENSE` absent du dépôt, et demandant l'ajout du texte de licence. Enregistrer le lien de l'issue dans le `README.md`, section Licence.

- [ ] **Step 6: Commit**

```bash
git add README.md docs/hardware/pinout.md docs/superpowers/specs/
git commit -m "docs: resultat du jalon 1 et pinout de la K-Touch"
```

---

## Notes de revue

Relecture du plan face à la spec, effectuée après rédaction.

**Couverture de la spec.** Les cinq critères de succès du jalon 1 sont couverts : sauvegarde de 16 777 216 octets et table de partitions conforme (tâche 5, étapes 4 et 5) ; démarrage depuis `app1` (tâche 6, étape 5) ; rétroéclairage et mire sans artefact (tâche 6, étape 6) ; coordonnées tactiles cohérentes dans le journal (tâche 6, étape 6) ; retour au stock effectué au moins une fois (tâche 7, étapes 1 et 2). La contrainte de non-modification de la table de partitions est portée par les contraintes globales et rappelée dans `partitions.csv`. La stratégie de sous-module qui évite toute redistribution est mise en œuvre à la tâche 4, étape 2, et l'issue de licence à la tâche 7, étape 5.

**Cohérence des noms.** `parse_app_desc`, `parse_partition_table`, `parse_image_header` et `NotAnEspImage` sont définis à la tâche 1 et consommés tels quels aux tâches 3 et 4. `active_slot`, `build_otadata`, `parse_otadata` et `seq_crc` sont définis à la tâche 2 et consommés aux tâches 3, 6 et 7. `STOCK_PARTITIONS` et `FLASH_SIZE` sont définis à la tâche 3 et utilisés par ses propres tests. `tests/test_dump.py` importe ses fixtures depuis `test_image.py`, ce que rend possible le `pythonpath`/`testpaths` de `pytest.ini`.

**Reports cosmétiques, à traiter au prochain jalon.** La vague de correction
finale a réintroduit deux points que le tour de correction de la tâche 3 avait
justement supprimés : `tests/test_dump.py` réimporte `struct` et `pytest` sans
les utiliser, et `tools/ktouch/dump.py` redéclare `OTADATA_SIZE` localement au
lieu de l'importer de `ktouch.otadata`. Les valeurs concordent, donc rien ne
casse ; c'est une régression de propreté, pas de comportement. Le processus
n'autorisant qu'une seule vague de correction après la revue finale, ces deux
points sont consignés ici plutôt que corrigés à chaud.

**Trois points mineurs assumés depuis les revues de tâches.** `PARTITION_MD5_MAGIC`
est défini mais inutilisé dans `image.py`, le magic de partition y est dépaqueté
deux fois, et le message d'erreur de troncature d'`otadata` annonce 8192 octets
alors que le minimum réellement accepté est 4128. La revue finale a jugé les
trois différables : dans chaque cas la direction d'échec est sûre, une table
tronquée faisant échouer la comparaison exacte avec le stock.

**Seule inconnue d'API assumée.** Le nom exact de la fonction d'initialisation du tactile (`pt_lvgl_touch_init`) doit être confirmé dans l'en-tête du BSP après l'ajout du sous-module ; la tâche 4, étape 3 le signale explicitement plutôt que de le supposer. C'est la seule dépendance du plan à un fichier qui n'est pas encore présent dans l'arborescence.
