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
