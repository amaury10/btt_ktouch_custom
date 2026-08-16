#!/usr/bin/env bash
# Regenere les captures d'ecran publiees dans README.md / README.en.md.
#
# Pourquoi un script et pas 27 invocations a la main : les images du README
# sont une affirmation publique sur ce que le firmware affiche. Si personne ne
# peut les reproduire, elles deviennent inverifiables des la premiere evolution
# d'un ecran. Ce fichier EST la recette -- relancer le script suffit a mettre
# la galerie a jour apres un changement d'IHM.
#
# Les PNG font 800x480 en RGB565, exactement le format que la dalle recoit :
# ce sont les pixels de l'appareil, pas une maquette (voir simulateur/README.md).
#
# Usage, depuis PowerShell :
#   wsl -d Debian -- bash "/mnt/e/.../tools/captures-readme.sh"
# ou, depuis un shell WSL, a la racine du depot :
#   ./tools/captures-readme.sh
set -euo pipefail

ici="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sortie="$ici/docs/captures"
sim="$ici/simulateur/run.sh"
mkdir -p "$sortie"

# Compile une fois avant la boucle : run.sh reconfigure et recompile a chaque
# appel, ce qui serait 27 no-op ninja au lieu d'un seul.
bash "$sim" --capture "$sortie/.build-warmup.png" --cycles 0 >/dev/null
rm -f "$sortie/.build-warmup.png"
exe="$ici/simulateur/build/ktouch-sim"

# Nombre de cycles par defaut. Il faut REMPLIR l'historique de temperature,
# pas seulement l'amorcer : le graphe de l'accueil trace KLIPPER_HISTO_POINTS
# points (120, voir klipper_temp_historique.h) et l'echantillonneur n'en pose
# qu'un par cycle. A 40 cycles la courbe tenait dans le tiers droit du cadre
# et l'accueil avait l'air d'un ecran mort.
#
# Attention au facteur 5 : l'echantillonneur ne retient pas un point par
# cycle, mais un point toutes les 5 secondes simulees (un cycle = une
# seconde). Remplir 120 points demande donc ~600 cycles, pas 120 -- c'est
# exactement le piege qui a produit une premiere serie de captures au graphe
# vide alors que le compte de cycles semblait largement suffisant.
CY=620

# capture <fichier> <arguments du simulateur...>
#
# --sans-bandeau : sans lui, la boucle de capture pose un "host connected" qui
#   RECOUVRE la rangee basse de l'ecran (le bandeau existe pour les captures de
#   revue, pas pour la documentation).
# --demo : peuple les six stores independants de l'imprimante (Bed Mesh, USB,
#   Spoolman, Parc, Console, Power), que le backend factice ne peut pas
#   alimenter -- sans lui ces ecrans ne montrent que leur etat vide.
capture() {
    local nom="$1"; shift
    "$exe" --capture "$sortie/$nom.png" --sans-bandeau --demo "$@" >/dev/null 2>&1
    printf '  %-26s %s\n' "$nom.png" "$*"
}

echo "Accueil et impression"
# Scenario 10 = "CR-10", mono-extrudeur au repos : la machine de reference du
# depot. Scenario 11 = "U1", changeur a quatre outils. Scenario 1 = impression
# en cours (c'est lui qui fait empiler ECRAN_ACCUEIL au lieu du hub).
capture accueil                --scenario 10 --cycles $CY
capture accueil-multi-outils   --scenario 11 --cycles $CY
capture impression             --scenario 1  --cycles $CY
capture fichiers               --scenario 10 --cycles $CY --ecran fichiers

echo "Actions et mouvement"
capture actions                --scenario 10 --cycles $CY --ecran actions
capture deplacer               --scenario 10 --cycles $CY --ecran deplacer
capture homing                 --scenario 10 --cycles $CY --ecran homing
capture macros                 --scenario 11 --cycles $CY --ecran macros
capture temperatures           --scenario 11 --cycles $CY --ecran temperatures
capture extruder               --scenario 11 --cycles $CY --ecran extruder
capture ventilateurs           --scenario 10 --cycles $CY --ecran ventilateurs
capture reglage-fin            --scenario 1  --cycles $CY --ecran fin
capture console                --scenario 10 --cycles $CY --ecran console

echo "Calibration et reglages"
capture menu-configuration     --scenario 10 --cycles $CY --ecran menu
capture zcalibrate             --scenario 10 --cycles $CY --ecran zcal
capture niveau-lit             --scenario 10 --cycles $CY --ecran lit
capture bed-mesh               --scenario 10 --cycles $CY --ecran bed_mesh
capture input-shaper           --scenario 10 --cycles $CY --ecran input_shaper
capture limites                --scenario 10 --cycles $CY --ecran limites
capture retraction             --scenario 10 --cycles $CY --ecran retraction
capture spoolman               --scenario 10 --cycles $CY --ecran spoolman

echo "Systeme"
capture wifi                   --scenario 10 --cycles $CY --ecran wifi
capture power                  --scenario 10 --cycles $CY --ecran power
capture usb                    --scenario 10 --cycles $CY --ecran usb
capture parc                   --scenario 10 --cycles $CY --ecran parc
capture updater                --scenario 10 --cycles $CY --ecran updater
# Scenario 7 = premier demarrage, ECRAN_CONFIGURATION a la place de l'accueil
# (ce que fait app_main.c quand reglages_configures() rend faux). --cycles 0 :
# rien n'est encore configure, il n'y a pas d'hote a joindre.
capture premier-demarrage      --scenario 7  --cycles 0

echo "Claviers et dialogues"
# Ces scenarios sont capture-only : rien ne simule le tactile hors fenetre, ils
# ouvrent donc directement la modale par-dessus l'ecran deja construit (voir
# simulateur/README.md, section --scenario).
capture clavier-texte          --scenario 5  --cycles $CY
capture confirmation           --scenario 6  --cycles $CY
capture clavier-temperature    --scenario 14 --cycles $CY
capture homing-confirmation    --scenario 13 --cycles $CY
capture premier-demarrage-saisie --scenario 8 --cycles 0

echo
echo "$(ls -1 "$sortie"/*.png | wc -l) captures dans docs/captures/"
