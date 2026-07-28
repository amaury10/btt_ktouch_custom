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

/* Tâche 9 : force (ou lève) l'échec de TOUTE commande par ailleurs valide
 * (pause/reprendre/annuler/arret_urgence) sur les appels suivants à
 * `commande()` -- rend ESP_FAIL au lieu de ESP_OK, sans toucher `etat`, sans
 * changer le comportement d'une action déjà inconnue (qui reste
 * ESP_ERR_NOT_SUPPORTED dans les deux cas). Sert à exercer, dans le
 * simulateur et le harnais de tests hôte, le chemin d'échec ASYNCHRONE d'une
 * commande -- celui où ui_commander() a déjà rendu ESP_OK (acceptée en file)
 * mais où l'exécution réelle, plus tard par la boucle, échoue quand même :
 * backend_factice.c ne produisant par ailleurs jamais d'échec de commande
 * (contrairement à rafraichir(), voir le backend jouet "echec-demo" de
 * simulateur/main.c pour l'équivalent côté rafraîchissement), rien d'autre ne
 * fait naturellement emprunter ce chemin. `false` restaure le comportement
 * normal (toutes les actions connues réussissent). */
void backend_factice_commande_echoue(bool echoue);
