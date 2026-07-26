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
