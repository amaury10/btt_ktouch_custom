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
