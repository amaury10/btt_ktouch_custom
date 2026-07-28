#pragma once

/* Petite fonction pure, extraite de web.c (revue tache 1, jalon 3a, fix
 * round 1, MAJOR) : borne un nombre de macros a serialiser dans /state au
 * plafond reellement alloue par le tampon fixe etat_klipper_t::macros. Pas
 * de dependance ESP-IDF ici, deliberement -- c'est ce qui permet de la
 * tester au harnais hote alors que web.c lui-meme (esp_http_server.h) ne
 * s'y compile pas. */

#include <stdint.h>

/* Rend min(nb_macros, KLIPPER_MACROS_MAX). `nb_macros` vient d'un champ
 * etat_klipper_t rempli par un producteur (backend factice, analyseur
 * Moonraker, future souscription WS) qui n'est pas tenu de respecter cette
 * borne lui-meme -- c'est exactement le sens de `macros_tronquees` dans
 * etat_klipper.h : un producteur peut annoncer plus de macros qu'il n'y a
 * de place, et le champ existe pour le signaler honnetement plutot que de
 * fournir une garantie a la source. Le point d'usage (ici, la
 * serialisation JSON) doit donc se defendre lui-meme, jamais faire
 * confiance a la valeur amont. */
uint8_t web_nb_macros_serialisables(uint8_t nb_macros);
