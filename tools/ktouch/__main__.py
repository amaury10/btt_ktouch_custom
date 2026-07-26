"""Permet `python -m ktouch <sous-commande>` depuis le dossier `tools`."""

import sys

from ktouch.cli import main

raise SystemExit(main([__spec__.name.split(".")[0], *sys.argv[1:]]))
