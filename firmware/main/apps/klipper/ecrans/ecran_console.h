/* Ecran Console (feature "Console gcode", tache B -- integration ESP) :
 * remplace l'ancien stub "Requires gcode_response capture - not yet available"
 * (ecran_stub.c, symbole retire de STUBS() par cette meme tache) par une
 * vraie console gcode -- scrollback du flux `notify_gcode_response` + echos
 * locaux des commandes envoyees (store dedie console_log.h, tache A, cable
 * par moonraker_ws.c dans cette meme tache B), et une saisie au clavier
 * tactile pour envoyer une commande gcode arbitraire.
 *
 * Mise en page (742x436, meme repere que ecran_power.c/ecran_fichiers.c) : un
 * scrollback plein largeur en haut (conteneur scrollable + un unique label
 * multi-lignes, auto-scroll en bas a chaque mise a jour -- voir
 * ecran_console.c), une rangee de saisie en bas (champ cliquable -> clavier
 * tactile existant, meme patron que ecran_reglages_wifi.c/clavier.h, bouton
 * "Envoyer", bouton "Effacer" qui vide le SCROLLBACK -- console_log_effacer()
 * -- jamais le champ de saisie).
 *
 * `mettre_a_jour()` ne reconstruit le texte du scrollback que si
 * `console_log_t.generation` a change depuis le dernier appel (memorisee
 * dans le contexte) -- meme discipline "generation" que ecran_power.c.
 *
 * Envoi : taper au clavier -> valider pose le texte dans le champ (sans
 * envoyer) ; "Envoyer" echo localement (`console_log_ajouter(">> ...")`) puis
 * construit `{"script":"<echappe>"}` via `json_echapper_chaine()` (les
 * guillemets englobants sont ajoutes ICI, le helper ne les met pas -- voir le
 * commentaire de tete de json_util.h) et appelle
 * `ui_commander(BACKEND_ACTION_GCODE, ...)` -- meme transport que
 * `envoyer_gcode()` dans ecran_fichiers.c, mais construction manuelle (pas
 * cJSON) car la saisie utilisateur EST le seul point d'entree de ce jalon ou
 * l'echappement JSON est un vrai point de securite (texte libre,
 * contrairement a un nom de macro/fichier contraint). Le champ est vide apres
 * envoi (succes ou echec de construction -- une commande qui a echoue a
 * construire ne doit pas rester bloquee dans le champ indefiniment).
 *
 * `ecran_console_ctx_t` est expose (comme `ecran_power_ctx_t`) pour
 * d'eventuels tests hote futurs -- pas de suite dediee a ce jour, cette tache
 * se limite au cablage + a l'ecran. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "clavier.h"
#include "console_log.h"
#include "ecran.h"
#include "lvgl.h"

typedef struct ecran_console_ctx_s ecran_console_ctx_t;

struct ecran_console_ctx_s {
    lv_obj_t *zone;       /* conteneur scrollable du scrollback */
    lv_obj_t *zone_label; /* unique label multi-lignes, enfant de zone */

    lv_obj_t *champ;          /* bouton cliquable -- ouvre le clavier tactile */
    lv_obj_t *champ_label;    /* commande en attente, ou placeholder si vide */
    lv_obj_t *bouton_envoyer; /* echo + envoi de ctx->commande, puis vide le champ */
    lv_obj_t *bouton_effacer; /* console_log_effacer() -- vide le SCROLLBACK, pas le champ */

    /* Commande tapee au clavier, en attente d'envoi -- vide tant que rien n'a
     * ete valide au clavier, ou juste apres un envoi (voir le commentaire de
     * tete). Meme tampon que ce que clavier_ouvrir() rend au rappel. */
    char commande[CLAVIER_VALEUR_MAX];

    uint32_t derniere_generation; /* derniere console_log_t.generation vue */
    bool     premiere_maj;        /* force le premier redessin (generation peut deja valoir 0 dans le store) */
};

extern const ecran_desc_t ECRAN_CONSOLE;
