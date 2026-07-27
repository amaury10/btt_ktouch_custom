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
 * Le découpage se fait sur le DERNIER ':' de la chaîne : une adresse IPv6
 * littérale contient elle-même des ':', et splitter sur le premier casserait
 * ce cas précis.
 *
 * Adresse et port sont validés indépendamment l'un de l'autre :
 * - Si aucun ':' n'est trouvé, ou si la partie adresse ne tient pas dans
 *   sortie->adresse (BACKEND_HOTE_LONGUEUR_MAX octets, nul compris), la
 *   chaîne entière est jugée inexploitable : sortie->adresse est vidée et
 *   sortie->port retombe sur HOTE_PARSE_PORT_DEFAUT.
 * - Sinon l'adresse (éventuellement vide, ex. ":1234") est copiée telle
 *   quelle. Le port n'est accepté que s'il s'écrit en base 10, sans signe ni
 *   caractère superflu, et vaut entre 1 et 65535 ; sinon il retombe seul sur
 *   HOTE_PARSE_PORT_DEFAUT, sans affecter l'adresse déjà copiée.
 *
 * Rend vrai si `sortie->adresse` est non vide en sortie, c'est-à-dire si le
 * résultat décrit un hôte exploitable — même si le port, lui, a dû retomber
 * sur sa valeur par défaut. Rend faux sinon (chaîne inexploitable, ou adresse
 * vide dans la chaîne d'origine, ex. ":1234"). */
bool hote_parse(const char *chaine, backend_hote_t *sortie);
