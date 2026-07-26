"""Lanceur : rend le paquet `ktouch` accessible depuis la racine du dépôt."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "tools"))

from ktouch.cli import main  # noqa: E402  (après ajustement du chemin)

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
