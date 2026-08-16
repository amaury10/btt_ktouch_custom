/* Backend jouet (tache 11) : le plus petit backend possible, qui respecte le
 * contrat de core/backend.h sans jamais toucher une ligne de core/ ni de
 * ui/ -- la preuve, en quelques dizaines de lignes, que le socle heberge
 * reellement une application qui n'est pas Klipper. Aucune I/O : rafraichir()
 * incremente un compteur, rien de plus.
 *
 * Sert de modele au fork astro (voir README.md a cote de ce fichier). */
#pragma once

#include <stdint.h>

#include "backend.h"

#define JOUET_LIBELLE_MAX 32

/* Etat a deux champs, exactement ce que demande le brief : un compteur, un
 * libelle. Type concret (pas opaque) comme etat_klipper_t -- un backend
 * publie toujours la forme reelle de son etat, jamais void*, voir
 * core/backend.h. */
typedef struct {
    uint32_t compteur;
    char     libelle[JOUET_LIBELLE_MAX];
} etat_jouet_t;

const backend_desc_t *backend_jouet_desc(void);
