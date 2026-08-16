/* Analyse pure d'une chaîne "adresse:port" en un backend_hote_t.
 *
 * Aucune dépendance à la NVS ni à ESP-IDF au-delà des types portables déjà
 * utilisés par backend.h : ce fichier se compile et se teste sur PC comme le
 * reste de core/. reglages.c est le seul appelant sur cible ; il lui fournit
 * la chaîne lue depuis la clé NVS « hote » et se charge lui-même de la
 * journalisation — cette fonction n'en fait aucune. */
#pragma once

#include <stdbool.h>

#include "backend.h"

/* Port utilisé quand la chaîne ne fournit aucun port exploitable. Doit rester
 * identique à REGLAGES_PORT_DEFAUT (reglages.c), qui s'appuie dessus plutôt
 * que de dupliquer la valeur. */
#define HOTE_PARSE_PORT_DEFAUT 7125u

/* Découpe `chaine` ("adresse:port") et remplit `sortie`. Fonction pure : ni
 * état, ni E/S, ni journalisation.
 *
 * Rejets d'emblée, avant tout découpage (revue de la tâche 8, round 1 —
 * une chaîne qui contredit une de ces règles se lit comme un URL collé
 * depuis un navigateur ou une saisie accidentellement encadrée d'espaces,
 * jamais comme un hôte que l'utilisateur avait réellement l'intention de
 * saisir) :
 * - un espace de tête ou de fin (jamais tronqué silencieusement : une
 *   saisie qui contient un défaut visible doit être visiblement refusée) ;
 *   CETTE fonction ne rejette QUE ces espaces de BORDURE. Un espace À
 *   L'INTÉRIEUR de la partie adresse (ex. "my printer:7125") est accepté
 *   tel quel par hote_parse() : c'est à l'APPELANT de le refuser avant
 *   l'appel, comme le fait ecran_configuration_valider() (voir
 *   ecran_configuration.c, contient_espace(), qui balaie la chaîne entière
 *   AVANT tout découpage, précisément parce que hote_parse() ne le fait
 *   pas). Un fork qui écrit son propre écran de configuration doit faire de
 *   même ;
 * - un préfixe de schéma, détecté par la seule présence de "://" n'importe
 *   où dans la chaîne (ex. "http://192.168.1.50:7125", qui produirait sinon
 *   une adresse "http" et une URL Moonraker doublement préfixée).
 *
 * Deux formes ensuite, selon que `chaine` commence par '[' ou non :
 * - Forme entre crochets "[adresse]" ou "[adresse]:port" (RFC 3986 §3.2.2) :
 *   la SEULE forme exploitable pour une adresse qui contient elle-même des
 *   ':' (IPv6 littéral). Les crochets sont retirés pour le stockage dans
 *   sortie->adresse (backend_moonraker.c les remet au moment de construire
 *   l'URL, voir son commentaire). Un crochet ouvrant jamais refermé, ou un
 *   contenu vide entre crochets, rend la chaîne entière inexploitable.
 * - Forme "adresse:port" classique, découpée sur le DERNIER ':' de la
 *   chaîne — MAIS si la partie adresse ainsi obtenue contient elle-même un
 *   ':', la chaîne est jugée AMBIGUË et rejetée entièrement plutôt que
 *   d'accepter une adresse arbitraire (avant la revue de la tâche 8,
 *   "fe80::1:8080" était accepté avec pour adresse "fe80::1" ; c'est
 *   exactement cette ambiguïté — indiscernable de "a:b:c", adresse "a:b" —
 *   que la forme entre crochets ci-dessus existe pour lever). Si aucun ':'
 *   n'est trouvé du tout, ou si la partie adresse ne tient pas dans
 *   sortie->adresse (BACKEND_HOTE_LONGUEUR_MAX octets, nul compris), la
 *   chaîne entière est également jugée inexploitable.
 *
 * Dans les deux formes, une fois l'adresse obtenue (éventuellement vide,
 * ex. ":1234") : le port n'est accepté que s'il s'écrit en base 10, sans
 * signe ni caractère superflu, et vaut entre 1 et 65535 ; sinon il retombe
 * seul sur HOTE_PARSE_PORT_DEFAUT, sans affecter l'adresse déjà copiée ni
 * rejeter la chaîne.
 *
 * Rend vrai si `sortie->adresse` est non vide en sortie, c'est-à-dire si le
 * résultat décrit un hôte exploitable — même si le port, lui, a dû retomber
 * sur sa valeur par défaut. Rend faux sinon (chaîne inexploitable au sens
 * ci-dessus, ou adresse vide dans la chaîne d'origine, ex. ":1234"). Sur
 * refus, sortie->adresse est TOUJOURS vidée et sortie->port TOUJOURS remis à
 * HOTE_PARSE_PORT_DEFAUT — jamais une valeur partiellement construite. */
bool hote_parse(const char *chaine, backend_hote_t *sortie);
