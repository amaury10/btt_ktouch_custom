/* Implementation : voir rail_actions.h pour le contrat. */
#include "rail_actions.h"

#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "confirmation.h"
#include "ecran_macros.h"
#include "klipper_gcode.h"
#include "navigation.h"
#include "source_etat.h"

/* Tampon suffisant pour {"script":"<gcode>"} -- meme raisonnement que
 * GCODE_ARGS_MAX dans ecran_deplacer.c (KLIPPER_GCODE_MAX + marge du wrapper
 * JSON et de l'echappement des `\n` reels). Les scripts du rail (G28, M112)
 * sont courts, mais on garde la meme marge que les ecrans par coherence. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode()/envoyer_gcode() dans ecran_deplacer.c (voir son
 * commentaire complet : un vrai octet 0x0A dans un script doit etre echappe
 * par un JSON conforme, jamais un snprintf a la main). Copie plutot que
 * partage, meme choix que le reste de ce depot. */
static bool construire_arguments_gcode(const char *script, char *sortie, size_t taille)
{
    if (script == NULL || sortie == NULL || taille == 0) {
        return false;
    }
    cJSON *racine = cJSON_CreateObject();
    if (racine == NULL) {
        return false;
    }
    if (cJSON_AddStringToObject(racine, "script", script) == NULL) {
        cJSON_Delete(racine);
        return false;
    }
    char *texte = cJSON_PrintUnformatted(racine);
    cJSON_Delete(racine);
    if (texte == NULL) {
        return false;
    }
    size_t longueur = strlen(texte);
    bool ok = longueur < taille;
    if (ok) {
        memcpy(sortie, texte, longueur + 1);
    }
    cJSON_free(texte);
    return ok;
}

static void envoyer_gcode(const char *script)
{
    if (script == NULL) {
        return;
    }
    char arguments[GCODE_ARGS_MAX];
    if (!construire_arguments_gcode(script, arguments, sizeof(arguments))) {
        return; /* ne devrait jamais arriver : GCODE_ARGS_MAX suffit toujours */
    }
    ui_commander(BACKEND_ACTION_GCODE, arguments);
}

/* Rappel de la confirmation d'arret d'urgence : n'envoie M112 que si
 * l'utilisateur a REELLEMENT confirme (voir confirmation.h : le rappel est
 * appele exactement une fois, dialogue deja referme). Un declin (confirme ==
 * false) ne fait rien -- un arret d'urgence ne doit jamais partir d'un
 * effleurement accidentel. */
static void rappel_stop(bool confirme, void *ctx)
{
    (void)ctx;
    if (!confirme) {
        return;
    }
    char s[KLIPPER_GCODE_MAX];
    if (klipper_gcode_arret_urgence(s, sizeof(s))) {
        envoyer_gcode(s);
    }
}

void rail_action_klipper(rail_action_t action, void *ctx)
{
    (void)ctx;
    switch (action) {
    case RAIL_ACCUEIL:
        /* Depile jusqu'a l'ecran d'accueil (fond de pile) -- generique, sans
         * savoir QUEL ecran est en fond : c'est app_main.c qui empile
         * ECRAN_ACCUEIL_HUB au demarrage. */
        navigation_accueil();
        break;
    case RAIL_BACK:
        /* Remonte d'UN seul niveau de navigation -- meme effet que le bouton
         * retour de la barre du haut (bouton_retour_cb() dans habillage.c),
         * simplement accessible en permanence depuis le rail. Le rail ne fait
         * plus de homing (RAIL_HOME retire du plan, voir rail.h) : le homing
         * des axes vit desormais uniquement dans ses ecrans dedies. */
        navigation_depiler();
        break;
    case RAIL_MACROS: {
        /* Garde anti-re-empilement (revue finale de branche, finding I1) : le
         * rail etant persistant et Macros atteignable DEPUIS Macros lui-meme,
         * re-taper le bouton (pourtant deja surligne "actif") empilerait des
         * copies de ECRAN_MACROS jusqu'a la borne de profondeur, forcant
         * ensuite plusieurs "Retour" pour en sortir. No-op si Macros est deja
         * au sommet -- l'utilisateur voit alors le bouton comme un simple
         * indicateur d'ecran courant, pas comme un empileur. */
        const char *courant = navigation_id_courant();
        if (courant != NULL && strcmp(courant, ECRAN_MACROS.id) == 0) {
            break;
        }
        /* Empile l'ecran macros par-dessus l'accueil. Echec (pile pleine)
         * delibrement ignore, meme choix que menu_deplacer_cb() dans
         * ecran_accueil_hub.c : rien d'utile a journaliser de plus qu'un
         * "rien ne se passe", la pile est bornee et loin de sa borne en
         * usage normal. */
        navigation_empiler(&ECRAN_MACROS);
        break;
    }
    case RAIL_STOP:
        /* Arret d'urgence PROTEGE par une confirmation (destructif = true :
         * bouton rouge, aucun etat par defaut) -- l'envoi reel de M112 se
         * fait dans rappel_stop(), sur confirmation seulement. */
        confirmation_ouvrir("Emergency stop?", "This will immediately halt the printer.", "STOP",
                            /*destructif=*/true, rappel_stop, NULL);
        break;
    case RAIL_NB:
    default:
        /* Aucune action pour une valeur hors enumeration -- defensif, ne
         * devrait jamais arriver (le rail ne dispatche que 0..RAIL_NB-1). */
        break;
    }
}
