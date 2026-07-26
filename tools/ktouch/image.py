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
