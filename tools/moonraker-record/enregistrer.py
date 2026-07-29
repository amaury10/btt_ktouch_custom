#!/usr/bin/env python3
"""Enregistreur de transcripts Moonraker WebSocket (tâche 4, jalon 3a).

Les tests de la tâche 3 (host-test/tests/test_moonraker_rpc.c) encodent
notre lecture de la documentation Moonraker. Ce script enregistre une
conversation RÉELLE avec un vrai Moonraker (adossé à un Klipper à MCU
simulé — voir docs/dev/klipper-simule.md) : chaque message entrant/sortant
est écrit tel quel, une ligne JSON par message, dans un fichier .jsonl que
le harnais hôte rejoue ensuite (voir la section "rejeu des fixtures" de
test_moonraker_rpc.c). Le protocole lui-même reste sous la responsabilité
de moonraker_rpc.c : ce script ne fait qu'observer et journaliser, il ne
réimplémente aucune règle de fusion/classification.

Usage (voir README.md du répertoire pour les détails d'installation) :

    python3 enregistrer.py idle       --sortie ../../host-test/fixtures/moonraker/connexion-idle.jsonl
    python3 enregistrer.py chauffe    --sortie ../../host-test/fixtures/moonraker/chauffe-buse.jsonl
    python3 enregistrer.py macro-ok   --sortie ../../host-test/fixtures/moonraker/macro-ok.jsonl
    python3 enregistrer.py macro-ko   --sortie ../../host-test/fixtures/moonraker/macro-inexistante.jsonl

Format d'une ligne du .jsonl (un objet JSON complet par ligne) :

    {"dir": "out"|"in", "t_ms": <entier, depuis la connexion>,
     "raw": "<texte EXACT envoyé/reçu sur le fil>",
     "message": <le même texte, déjà décodé -- absent si illisible>}

`raw` est la source de vérité (c'est elle que le harnais hôte relit et
repasse aux fonctions de moonraker_rpc.c) ; `message` n'est là que pour le
confort de lecture humaine du fichier.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path

try:
    import websockets
except ImportError:
    sys.exit(
        "Le paquet 'websockets' est introuvable -- avez-vous active le venv "
        "local ? Voir README.md (python3 -m venv .venv puis pip install -r "
        "requirements.txt)."
    )

# ----------------------------------------------------------------------
# Construction des requêtes -- SYNCHRONISÉ À LA MAIN avec
# firmware/main/apps/klipper/moonraker_rpc.c. Si l'un des deux change,
# l'autre DOIT changer aussi, sinon les fixtures enregistrées ici cessent
# de représenter ce que le firmware envoie réellement.
# ----------------------------------------------------------------------

# Gabarit EXACT de rpc_construire_requete() (moonraker_rpc.c:24-30) :
#   snprintf(..., "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":%u}", ...)
#   snprintf(..., "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"id\":%u}", ...)          (params NULL)
def requete_jsonrpc(id_: int, methode: str, params_json: str | None = None) -> str:
    if params_json is not None:
        return (
            '{"jsonrpc":"2.0","method":"%s","params":%s,"id":%d}'
            % (methode, params_json, id_)
        )
    return '{"jsonrpc":"2.0","method":"%s","id":%d}' % (methode, id_)


# Copie caractère pour caractère de la constante PARAMS de
# rpc_construire_abonnement() (moonraker_rpc.c:54-61). Ne PAS reformater
# (pas d'espaces, même ordre de clés) : c'est précisément ce genre de
# divergence "cosmétique" qui a fait confondre '/'  et '.' dans le nom de
# méthode lors de la revue de la tâche 3.
SUBSCRIBE_PARAMS = (
    '{"objects":{'
    '"toolhead":null,"gcode_move":null,'
    '"extruder":null,"extruder1":null,"extruder2":null,"extruder3":null,'
    '"extruder4":null,"extruder5":null,"extruder6":null,"extruder7":null,'
    '"heater_bed":null,"fan":null,'
    '"print_stats":null,"virtual_sdcard":null,"webhooks":null'
    "}}"
)


def requete_abonnement(id_: int) -> str:
    """Reproduit rpc_construire_abonnement() -- méthode "printer.objects.subscribe",
    avec des POINTS (jamais de '/', voir le commentaire de tête de
    moonraker_rpc.c sur ce piège précis)."""
    return requete_jsonrpc(id_, "printer.objects.subscribe", SUBSCRIBE_PARAMS)


def requete_identify(id_: int) -> str:
    """server.connection.identify n'est PAS construite par moonraker_rpc.c
    (la tâche 3 n'a implémenté que l'abonnement et les requêtes génériques) :
    ce gabarit suit uniquement la documentation Moonraker publique. Un nom de
    client neutre suffit -- Moonraker ne l'utilise que pour ses logs/UI."""
    params = (
        '{"client_name":"ktouch-moonraker-record",'
        '"version":"0.1",'
        '"type":"other",'
        '"url":"https://github.com/bigtreetech/K-Touch"}'
    )
    return requete_jsonrpc(id_, "server.connection.identify", params)


ID_IDENTIFY = 1
ID_SUBSCRIBE = 2
ID_ACTION = 3


# ----------------------------------------------------------------------
# Enregistrement
# ----------------------------------------------------------------------


class Enregistreur:
    """Journalise chaque message envoyé/reçu, un objet JSON par ligne,
    horodaté relativement à l'instant de connexion (pas d'horodatage absolu
    -- inutile au rejeu, et ça éviterait d'avoir à le neutraliser pour que
    deux enregistrements du même scénario soient diff-ables)."""

    def __init__(self, chemin: Path):
        self.debut = time.monotonic()
        self.chemin = chemin
        self.chemin.parent.mkdir(parents=True, exist_ok=True)
        self.fichier = chemin.open("w", encoding="utf-8")
        self.nb_lignes = 0

    def _t_ms(self) -> int:
        return int((time.monotonic() - self.debut) * 1000)

    def _consigner(self, direction: str, texte: str) -> None:
        enveloppe = {"dir": direction, "t_ms": self._t_ms(), "raw": texte}
        try:
            enveloppe["message"] = json.loads(texte)
        except json.JSONDecodeError:
            pass  # `raw` seul suffit ; un message illisible reste consignable
        self.fichier.write(json.dumps(enveloppe, ensure_ascii=False) + "\n")
        self.fichier.flush()
        self.nb_lignes += 1
        resume = texte if len(texte) <= 160 else texte[:157] + "..."
        print(f"  [{direction}] {resume}")

    async def envoyer(self, ws, texte: str) -> None:
        await ws.send(texte)
        self._consigner("out", texte)

    async def recevoir(self, ws, timeout: float | None = None) -> str:
        if timeout is None:
            texte = await ws.recv()
        else:
            texte = await asyncio.wait_for(ws.recv(), timeout=timeout)
        self._consigner("in", texte)
        return texte

    def fermer(self) -> None:
        self.fichier.close()
        print(f"-> {self.nb_lignes} message(s) écrit(s) dans {self.chemin}")


async def connecter_et_abonner(ws, rec: Enregistreur) -> None:
    await rec.envoyer(ws, requete_identify(ID_IDENTIFY))
    await rec.recevoir(ws)
    await rec.envoyer(ws, requete_abonnement(ID_SUBSCRIBE))
    await rec.recevoir(ws)


def _extraire_extruder_statut(texte_json: str) -> dict | None:
    try:
        msg = json.loads(texte_json)
    except json.JSONDecodeError:
        return None
    statut = None
    if msg.get("method") == "notify_status_update":
        params = msg.get("params") or []
        if params and isinstance(params[0], dict):
            statut = params[0]
    elif "result" in msg and isinstance(msg.get("result"), dict):
        statut = msg["result"].get("status")
    if not isinstance(statut, dict):
        return None
    extr = statut.get("extruder")
    return extr if isinstance(extr, dict) else None


# ----------------------------------------------------------------------
# Scénarios
# ----------------------------------------------------------------------


async def scenario_idle(ws, rec: Enregistreur, duree: float) -> None:
    await connecter_et_abonner(ws, rec)
    fin = time.monotonic() + duree
    while True:
        restant = fin - time.monotonic()
        if restant <= 0:
            break
        try:
            await rec.recevoir(ws, timeout=restant)
        except asyncio.TimeoutError:
            break


async def scenario_chauffe(ws, rec: Enregistreur, duree_max: float) -> None:
    """Constat empirique (voir docs/dev/klipper-simule.md, section
    "limite connue") : le MCU simulé de virtual-klipper-printer renvoie une
    lecture ADC de thermistance CONSTANTE (`analog_in_state` = mêmes octets
    avant/après la commande, vérifié dans les logs du conteneur) -- il n'y a
    aucun modèle thermique, `temperature` ne "monte" donc jamais dans ce
    simulateur, quelle que soit la durée d'attente. Le protocole reste
    fidèlement observable pour ce qui EST réel : la consigne (`target`)
    change bel et bien, poussée par un notify_status_update. Ce scénario
    s'arrête dès que ce push est vu, plutôt que d'attendre une montée de
    température qui ne viendra jamais -- une fenêtre d'attente courte est
    aussi ce qui protège le mieux contre l'instabilité du MCU simulé
    ("Timer too close", sensible à la gigue d'ordonnancement de l'hôte,
    voir le même fichier de doc) : plus l'enregistrement dure, plus le
    risque d'un crash du MCU pendant la capture augmente."""
    await connecter_et_abonner(ws, rec)

    await rec.envoyer(
        ws,
        requete_jsonrpc(
            ID_ACTION,
            "printer.gcode.script",
            '{"script":"SET_HEATER_TEMPERATURE HEATER=extruder TARGET=60"}',
        ),
    )

    fin = time.monotonic() + duree_max
    cible_confirmee = False
    while time.monotonic() < fin:
        restant = fin - time.monotonic()
        try:
            texte = await rec.recevoir(ws, timeout=restant)
        except asyncio.TimeoutError:
            break
        extr = _extraire_extruder_statut(texte)
        if extr is not None and extr.get("target") == 60.0:
            cible_confirmee = True
            break

    if not cible_confirmee:
        print(
            "AVERTISSEMENT : le push de la nouvelle consigne (target=60) "
            f"n'est pas confirmé dans les {duree_max:.0f} s -- le fixture "
            "risque de ne pas satisfaire le critère 'consigne == 60' du "
            "rejeu (MCU simulé instable ? voir docs/dev/klipper-simule.md).",
            file=sys.stderr,
        )

    # Nettoyage : on éteint le chauffage APRÈS avoir fermé l'enregistrement
    # (voir main()) -- le fixture doit se terminer avec la consigne à 60,
    # comme l'exige le rejeu, pas avec le retour à 0 qui n'est qu'une
    # précaution pour ne pas laisser le simulateur en chauffe.


async def _lire_jusqua_id(ws, rec: Enregistreur, id_attendu: int, timeout_total: float) -> None:
    fin = time.monotonic() + timeout_total
    while time.monotonic() < fin:
        restant = fin - time.monotonic()
        try:
            texte = await rec.recevoir(ws, timeout=restant)
        except asyncio.TimeoutError:
            return
        try:
            msg = json.loads(texte)
        except json.JSONDecodeError:
            continue
        if msg.get("id") == id_attendu:
            return


async def scenario_macro(ws, rec: Enregistreur, script: str, timeout: float) -> None:
    await connecter_et_abonner(ws, rec)
    params = json.dumps({"script": script}, separators=(",", ":"))
    await rec.envoyer(ws, requete_jsonrpc(ID_ACTION, "printer.gcode.script", params))
    await _lire_jusqua_id(ws, rec, ID_ACTION, timeout)


async def eteindre_extrudeur(url: str) -> None:
    """Remet la cible de l'extrudeur à 0 -- appelé APRÈS la fermeture du
    fichier d'enregistrement (connexion séparée, non journalisée) pour ne
    pas polluer le fixture chauffe-buse avec la redescente."""
    async with websockets.connect(url) as ws:
        await ws.send(
            requete_jsonrpc(
                999, "printer.gcode.script", '{"script":"SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0"}'
            )
        )
        try:
            await asyncio.wait_for(ws.recv(), timeout=5.0)
        except asyncio.TimeoutError:
            pass


# ----------------------------------------------------------------------
# main
# ----------------------------------------------------------------------

SCENARIOS = {
    "idle": "connexion + abonnement + ~30 s d'inactivité (connexion-idle.jsonl)",
    "chauffe": "chauffe de l'extrudeur a 60C, cible reelle Klipper (chauffe-buse.jsonl)",
    "macro-ok": "execution d'une macro existante, LOAD_FILAMENT (macro-ok.jsonl)",
    "macro-ko": "execution d'une macro inexistante -- erreur Klipper reelle (macro-inexistante.jsonl)",
}


async def executer(args: argparse.Namespace) -> None:
    rec = Enregistreur(Path(args.sortie))
    try:
        async with websockets.connect(args.url) as ws:
            if args.scenario == "idle":
                await scenario_idle(ws, rec, args.duree)
            elif args.scenario == "chauffe":
                await scenario_chauffe(ws, rec, args.duree)
            elif args.scenario == "macro-ok":
                await scenario_macro(ws, rec, "LOAD_FILAMENT", args.duree)
            elif args.scenario == "macro-ko":
                await scenario_macro(ws, rec, "MACRO_QUI_NEXISTE_PAS", args.duree)
    finally:
        rec.fermer()

    if args.scenario == "chauffe":
        print("Nettoyage : redescente de la cible extrudeur a 0C (non journalisee)...")
        await eteindre_extrudeur(args.url)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Enregistre un transcript JSON-RPC Moonraker reel (voir docs/dev/klipper-simule.md).",
        epilog="Scenarios disponibles :\n"
        + "\n".join(f"  {nom:10s} {desc}" for nom, desc in SCENARIOS.items()),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("scenario", choices=sorted(SCENARIOS.keys()))
    parser.add_argument(
        "--url", default="ws://localhost:7125/websocket", help="URL WebSocket de Moonraker"
    )
    parser.add_argument("--sortie", required=True, help="Chemin du fichier .jsonl a ecrire")
    parser.add_argument(
        "--duree",
        type=float,
        default=None,
        help="Duree en secondes (defaut : 30 pour idle, 60 pour chauffe/macro-*)",
    )
    args = parser.parse_args()
    if args.duree is None:
        args.duree = 30.0 if args.scenario == "idle" else 60.0

    print(f"Scenario '{args.scenario}' -> {args.url}")
    asyncio.run(executer(args))


if __name__ == "__main__":
    main()
