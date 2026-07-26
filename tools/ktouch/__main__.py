"""Point d'entrée : `python ktouch.py <sauvegarde.bin>` depuis la racine."""

import sys

from ktouch.dump import inspect_dump


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage : python ktouch.py <sauvegarde.bin>", file=sys.stderr)
        return 2
    try:
        with open(argv[1], "rb") as handle:
            data = handle.read()
    except OSError as erreur:
        print(f"lecture impossible : {erreur}", file=sys.stderr)
        return 2
    report = inspect_dump(data)
    print(report.format())
    return 0 if report.safe_to_flash else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
