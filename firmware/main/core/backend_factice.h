/* Backend synthétique : produit un état plausible sans aucune machine.
 *
 * Il sert à trois choses. Faire tourner le simulateur sur PC. Exercer les cas
 * pénibles qu'une vraie imprimante ne produit pas à la demande — température
 * nulle, impression à 99 %, valeurs aberrantes. Et surtout garantir que le
 * socle a en permanence deux consommateurs réels de son abstraction, puisque
 * l'application astro vit dans un fork. */
#pragma once

#include "backend.h"
#include "etat_klipper.h"

const backend_desc_t *backend_factice_desc(void);

/* 0 repos · 1 impression qui progresse · 2 pause · 3 valeurs extrêmes
 * (plausibles) · 4 valeurs aberrantes (hors plage -- vérifie qu'un
 * affichage rend "--" plutôt qu'un nombre faux, voir ui_format_temperature()
 * dans firmware/main/ui/widgets/tuile.h) */
void backend_factice_scenario(int numero);
