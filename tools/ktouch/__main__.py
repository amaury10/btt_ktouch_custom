"""Point d'entrée : `python -m ktouch <sauvegarde.bin>`."""

import sys

from ktouch.dump import inspect_dump


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage : python -m ktouch <sauvegarde.bin>", file=sys.stderr)
        return 2
    with open(argv[1], "rb") as handle:
        report = inspect_dump(handle.read())
    print(report.format())
    return 0 if report.safe_to_flash else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
