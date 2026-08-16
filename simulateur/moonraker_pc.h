/* Backend Moonraker PC (tache 7, jalon 3a) : parle en HTTP a un VRAI
 * Moonraker depuis le simulateur, via un GET sockets POSIX nu -- PAS
 * esp_http_client (reserve a l'ESP, voir backend_moonraker.c), PAS libcurl
 * (nouvelle dependance refusee sans decision utilisateur, voir le brief de
 * la tache).
 *
 * Alimente moonraker_parse_status() (moonraker_parse.c) -- la MEME fonction
 * pure que le backend ESP et que les fixtures de la tache 4 -- avec une
 * vraie reponse /printer/objects/query. C'est donc le chemin HTTP de
 * backend_moonraker.c qui est valide ici, PAS son chemin WebSocket
 * (moonraker_ws.c reste ESP-only, YAGNI cote PC tant qu'aucun besoin
 * concret ne l'exige -- les fixtures + l'ESP suffisent a le couvrir).
 *
 * Second GET (best-effort, /printer/objects/list) alimente rpc_lire_macros()
 * (moonraker_rpc.c) pour que ECRAN_MACROS ait quelque chose a montrer contre
 * une vraie machine -- un cycle dont ce second GET echoue reste un succes
 * (l'etat/temperature/impression restent exploitables sans la liste des
 * macros), seul le premier GET (le statut) engage le succes du cycle. */
#pragma once

#include "backend.h"

/* A appeler AVANT source_etat_sim_demarrer() : source_etat_sim.c appelle
 * TOUJOURS desc->demarrer(initial, NULL) (voir son commentaire de tete,
 * source_etat_sim.h) -- aucun hote reel ne transite jamais par ce chemin
 * cote simulateur. Ce setter est donc le SEUL moyen de communiquer l'hote
 * choisi par --hote a ce backend. Copie `*hote` (pas de duree de vie a
 * respecter par l'appelant apres le retour). */
void moonraker_pc_definir_hote(const backend_hote_t *hote);

const backend_desc_t *moonraker_pc_desc(void);
