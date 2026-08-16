/* rail_actions.h — le comportement Klipper du rail persistant (jalon refonte
 * accueil/deplacer, tache 6 : integration).
 *
 * rail.h (ui/widgets) reste generique : il ne fait NI navigation NI gcode, il
 * ne fait que dispatcher `sur_action(action, ctx)` (voir son commentaire de
 * tete). CE fichier-ci est la couche APPLICATION qui decide quoi faire de
 * chaque action -- exactement le meme partage que les ecrans (ecran_deplacer.c
 * appelle klipper_gcode.h/ui_commander(), pas ui/). Il est branche sur
 * l'habillage generique via habillage_definir_action_rail() (voir
 * ui/habillage.h) au demarrage de l'application (app_main.c) :
 *
 *   RAIL_BACK    -> navigation_depiler()           (remonte d'un niveau)
 *   RAIL_ACCUEIL -> navigation_accueil()          (revient a l'ecran d'accueil)
 *   RAIL_MACROS  -> navigation_empiler(&ECRAN_MACROS)
 *   RAIL_STOP    -> confirmation, puis M112 (klipper_gcode_arret_urgence())
 *
 * Compile en host-test ET en firmware (comme les ecrans) : la logique
 * "action du rail -> gcode/navigation" est ainsi testable sur PC, sans
 * materiel (voir host-test/tests/test_integration_rail.c). */
#pragma once

#include "rail.h" /* rail_action_t */

/* Handler a passer a habillage_definir_action_rail() : traduit une action du
 * rail en navigation et/ou commande Klipper (voir le tableau ci-dessus).
 * `ctx` est ignore (NULL a l'enregistrement) -- la couche Klipper n'a pas
 * d'etat propre a porter ici, tout passe par navigation_*() et ui_commander()
 * (des singletons process-wide). Une action hors de l'enumeration
 * rail_action_t (dont RAIL_NB) ne fait rien. */
void rail_action_klipper(rail_action_t action, void *ctx);
