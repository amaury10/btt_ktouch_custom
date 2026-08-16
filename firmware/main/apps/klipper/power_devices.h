/* Store dédié de la liste des prises pilotées par Moonraker (`[power ...]`,
 * API `machine.device_power.*`).
 *
 * POURQUOI un store à part, HORS de `etat_klipper_t`, sur le patron EXACT de
 * klipper_fichiers.h (voir son commentaire de tête pour le détail complet) :
 * `etat_klipper_t` est un POD copié PARTOUT -- copies statiques en RAM
 * interne (g_etat de l'habillage, g_dernier_etat_ws du backend, la boîte
 * moonraker_boite) ET posé sur les piles des tâches WS/boucle/httpd. Ajouter
 * la liste des prises DANS cet état multiplierait sa taille par autant de
 * copies qu'il en existe, exactement le défaut qui avait épuisé la RAM
 * interne au point que la tâche WebSocket ne pouvait plus s'allouer (« Error
 * create websocket task ») -- voir la mémoire du projet. Ce store vit donc en
 * EXACTEMENT une instance, comme klipper_fichiers.c.
 *
 * Le store est ÉCRIT par la tâche WebSocket (power_devices_definir() sur
 * réponse à machine.device_power.devices, power_devices_maj_un() sur
 * notify_power_changed) et LU par d'autres tâches (l'écran Power via la
 * tâche LVGL) : l'accès est donc protégé par un verrou (voir
 * power_devices.c). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define POWER_DEVICES_MAX 8
#define POWER_NOM_MAX      32

/* Une prise Moonraker connue. `nom` est le nom `[power NOM]` tel que rendu
 * par Moonraker (identifiant de section, pas un libellé humain). */
typedef struct {
    char nom[POWER_NOM_MAX];
    bool allumee; /* status == "on" */
    bool connue;  /* true dès qu'un état a été reçu pour cette prise (sinon
                   * inconnue -- ne pas confondre avec allumee == false) */
} power_device_t;

/* Instantané complet de la liste des prises. Les entrées au-delà de `nb`
 * sont sans signification. */
typedef struct {
    power_device_t devices[POWER_DEVICES_MAX];
    uint8_t nb;         /* 0 = liste jamais reçue ou vide */
    bool tronque;        /* true si Moonraker en avait plus que POWER_DEVICES_MAX */
    uint32_t generation; /* incrémentée à chaque écriture (definir() ou
                          * maj_un()) -- permet à l'UI de détecter qu'il y a du
                          * neuf sans comparer le contenu champ par champ */
} power_devices_t;

/* Remplace ENTIÈREMENT le contenu du store par `*src` (copie sous verrou),
 * et incrémente `generation` (celle de `*src` est ignorée -- le store gère
 * lui-même son compteur). `src` NULL = no-op. */
void power_devices_definir(const power_devices_t *src);

/* Copie le contenu courant du store dans `*dest` (fourni par l'appelant, sous
 * verrou). `dest` NULL = no-op. */
void power_devices_lire(power_devices_t *dest);

/* Met à jour UNE prise déjà connue par son nom (comparaison exacte,
 * sensible à la casse -- même convention que le reste du protocole
 * Moonraker) : si `nom` correspond à une entrée existante, son `allumee` est
 * mis à `allumee` et `connue` à true, et `generation` est incrémentée. Si
 * `nom` ne correspond à AUCUNE entrée existante (prise non listée par le
 * fetch initial -- ne devrait pas arriver avec un Moonraker cohérent, mais
 * une notification peut en théorie précéder la réponse au fetch), l'appel
 * est un no-op silencieux : cette fonction met à jour une prise CONNUE, elle
 * n'en crée jamais. `nom` NULL = no-op. */
void power_devices_maj_un(const char *nom, bool allumee);

/* Compteur monotone du store, lu sans copier la liste des prises. Existe pour
 * generation_externe_klipper() (app_main.c), qui additionne les compteurs des
 * stores INDEPENDANTS de l'imprimante -- voir le commentaire de ce hook et
 * habillage.h. Meme contrat que bed_mesh_generation() / spoolman_generation() /
 * usb_fichiers_generation(). */
uint32_t power_devices_generation(void);
