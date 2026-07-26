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

ESP_OTA_IMG_VALID = 0x00000001


def esp_crc32_le(init: int, data: bytes) -> int:
    """Reproduit `esp_rom_crc32_le` : CRC-32 réfléchi sans inversion finale.

    zlib inverse le registre en entrée et en sortie ; on compense des deux côtés
    pour retrouver la valeur brute attendue par le bootloader.
    """
    return zlib.crc32(data, init ^ 0xFFFFFFFF) ^ 0xFFFFFFFF


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
        return self.ota_seq != ERASED and self.crc == seq_crc(self.ota_seq)


def parse_otadata(data: bytes) -> list[OtaEntry]:
    """Lit les deux copies de l'entrée, une par secteur."""
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
