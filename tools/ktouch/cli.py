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
