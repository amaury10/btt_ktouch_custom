/* Santé de la liaison avec l'hôte, exprimée en quatre états.
 *
 * L'habillage est seul à afficher cet état : un écran ne montre jamais de boîte
 * d'erreur réseau, il grise ses données périmées. Cette règle évite que chaque
 * panneau invente sa propre façon de dire « je n'ai pas de nouvelles ». */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LIAISON_CONNEXION = 0,  /* jamais joint l'hôte depuis le démarrage */
    LIAISON_EN_LIGNE,
    LIAISON_DEGRADEE,       /* des échecs, pas encore de quoi renoncer */
    LIAISON_HORS_LIGNE,
} liaison_etat_t;

typedef struct {
    liaison_etat_t etat;
    uint32_t       echecs_consecutifs;
    uint8_t        seuil_degrade;
    uint8_t        seuil_hors_ligne;
} liaison_t;

/* Seuils par défaut : 3 échecs consécutifs (~3 s à une interrogation par
 * seconde) pour distinguer une vraie dégradation d'un paquet isolé perdu sur
 * le réseau local, 10 (~10 s) pour signaler une coupure réelle sans réagir au
 * moindre creux passager d'un réseau WiFi domestique. Nommés ici plutôt que
 * cachés dans core/boucle.c (qui les utilise pour la liaison publique de
 * /state) pour qu'un consommateur indépendant de liaison_t partage la même
 * sensibilité sans dupliquer ces deux nombres — voir backend_moonraker.c, qui
 * s'en sert pour cadencer son propre journal d'échecs réseau. */
#define LIAISON_SEUIL_DEGRADE_DEFAUT     3
#define LIAISON_SEUIL_HORS_LIGNE_DEFAUT 10

/* Précondition : seuil_degrade DOIT être <= seuil_hors_ligne. liaison_echec()
 * (voir liaison.c) compare echecs_consecutifs à seuil_hors_ligne AVANT
 * seuil_degrade, dans cet ordre précis ; si l'appelant inverse les deux
 * arguments (seuil_degrade > seuil_hors_ligne), LIAISON_DEGRADEE devient
 * inatteignable — un compteur qui franchit seuil_hors_ligne prend directement
 * LIAISON_HORS_LIGNE, sans jamais passer par l'état intermédiaire. Ce n'est
 * pas vérifié ici (liaison_init() ne valide aucun argument, voir le
 * commentaire de contrat de etat_store.h pour la même convention) : l'échec
 * en cas d'inversion se dégrade vers l'état le plus sévère plutôt que vers un
 * comportement indéfini, ce qui reste défendable, mais un appelant qui
 * fournit ces deux seuils doit respecter cet ordre. */
void           liaison_init(liaison_t *l, uint8_t seuil_degrade, uint8_t seuil_hors_ligne);
void           liaison_succes(liaison_t *l);
void           liaison_echec(liaison_t *l);
liaison_etat_t liaison_etat(const liaison_t *l);
uint32_t       liaison_echecs_consecutifs(const liaison_t *l);
const char    *liaison_nom(liaison_etat_t etat);
