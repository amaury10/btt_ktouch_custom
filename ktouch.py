"""Lanceur : rend le paquet `ktouch` accessible depuis la racine du dépôt.

Le paquet vit sous `tools/` pour ne pas encombrer la racine, ce qui le rend
invisible à `python -m ktouch`. Ce lanceur ajoute `tools/` au chemin de
recherche pour que la commande de vérification, qui est le garde-fou de tout
le projet, s'exécute telle qu'elle est documentée.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "tools"))

from ktouch.__main__ import main  # noqa: E402  (après ajustement du chemin)

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
