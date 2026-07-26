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
from ktouch.otadata import OTADATA_SIZE, active_slot as read_active_slot

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
        """Vrai si la sauvegarde permet une restauration intégrale."""
        return self.size_ok and self.partitions_match_stock

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
