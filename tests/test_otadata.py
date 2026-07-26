"""Tests de la partition otadata, qui détermine le slot OTA au démarrage.

Le bootloader retient, parmi les entrées dont le CRC est correct, celle qui a le
`ota_seq` le plus élevé, puis démarre le slot `(ota_seq - 1) % nombre_de_slots`.
"""

import struct

import pytest

from ktouch.otadata import (
    OTADATA_SIZE,
    SECTOR_SIZE,
    OtaEntry,
    active_slot,
    build_otadata,
    esp_crc32_le,
    parse_otadata,
    seq_crc,
)


def reference_crc32(data: bytes, register: int = 0xFFFFFFFF) -> int:
    """CRC-32 réfléchi bit à bit, sans inversion finale.

    Implémentation volontairement naïve et indépendante de zlib : elle sert de
    témoin pour prouver que `esp_crc32_le` reproduit bien `esp_rom_crc32_le`.
    """
    for byte in data:
        register ^= byte
        for _ in range(8):
            register = (register >> 1) ^ (0xEDB88320 if register & 1 else 0)
    return register


def make_entry(ota_seq, ota_state=0x00000001, crc=None):
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
    assert esp_crc32_le(0xFFFFFFFF, payload) == reference_crc32(payload)


def test_seq_crc_valeurs_connues():
    """Vecteurs calculés puis recoupés avec l'implémentation bit à bit."""
    assert seq_crc(1) == 0x66074786
    assert seq_crc(2) == 0x74B2E868


def test_parse_otadata_lit_les_deux_copies():
    entries = parse_otadata(make_otadata(make_entry(1), make_entry(4)))
    assert entries[0] == OtaEntry(ota_seq=1, ota_state=1, crc=seq_crc(1))
    assert entries[1] == OtaEntry(ota_seq=4, ota_state=1, crc=seq_crc(4))


def test_entree_avec_crc_faux_est_invalide():
    entry = parse_otadata(make_otadata(make_entry(1, crc=0xDEADBEEF)))[0]
    assert entry.valid is False


def test_entree_effacee_est_invalide():
    """Une partition vierge (0xFF partout) ne désigne aucun slot."""
    assert parse_otadata(make_otadata())[0].valid is False


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


@pytest.mark.parametrize("slot", [-1, 2, 99])
def test_build_otadata_refuse_un_slot_hors_bornes(slot):
    with pytest.raises(ValueError):
        build_otadata(slot)
