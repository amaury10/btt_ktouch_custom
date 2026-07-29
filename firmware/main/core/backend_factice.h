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
 * dans firmware/main/ui/widgets/tuile.h) · 5-9 réservés au simulateur (voir
 * simulateur/README.md, ne correspondent à aucun scénario ici -- repli sur
 * le comportement du scénario 3) · 10 « CR-10 » (mono-extrudeur, plateau
 * chauffant, 4 macros simples) · 11 « U1 » (4 extrudeurs, outil_actif qui
 * tourne d'un cycle à l'autre, 8 macros dont une cachée/une à paramètres/une
 * qui échoue toujours à la commande) · 12 « 8 têtes » (8 extrudeurs, 48
 * macros -- KLIPPER_MACROS_MAX -- et macros_tronquées levé). Tout autre
 * numéro retombe sur le comportement du scénario 3 (voir backend_factice.c,
 * seule source de vérité pour cette numérotation). */
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

/* Remet à zéro l'état inter-appels processus (progression du scénario 1,
 * outil actif du scénario U1). Sans effet utile sur cible ; dans le harnais
 * hôte, une suite qui veut un point de départ déterministe l'appelle avant de
 * dérouler ses scénarios (revue finale jalon 3a). */
void backend_factice_reinit(void);
