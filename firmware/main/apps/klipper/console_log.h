/* Store dédié du scrollback de la console gcode (flux `notify_gcode_response`
 * + échos locaux des commandes envoyées par l'utilisateur).
 *
 * POURQUOI un store à part, HORS de `etat_klipper_t`, sur le patron EXACT de
 * klipper_fichiers.h/power_devices.h (voir leurs commentaires de tête pour le
 * détail complet) : `etat_klipper_t` est un POD copié PARTOUT -- copies
 * statiques en RAM interne (g_etat de l'habillage, g_dernier_etat_ws du
 * backend, la boîte moonraker_boite) ET posé sur les piles des tâches
 * WS/boucle/httpd. Ajouter le scrollback DANS cet état multiplierait sa
 * taille par autant de copies qu'il en existe, exactement le défaut qui avait
 * épuisé la RAM interne au point que la tâche WebSocket ne pouvait plus
 * s'allouer (« Error create websocket task ») -- voir la mémoire du projet.
 * Ce store vit donc en EXACTEMENT une instance, comme klipper_fichiers.c/
 * power_devices.c.
 *
 * Le store est ÉCRIT par la tâche WebSocket (console_log_ajouter() sur
 * réception de `notify_gcode_response`, découpée en lignes par l'appelant --
 * voir moonraker_ws.c, tâche B) ET par la tâche LVGL (écho local de la
 * commande tapée par l'utilisateur avant même l'envoi, voir ecran_console.c,
 * tâche B) ; il est LU par la tâche LVGL (rafraîchissement de l'écran
 * console) : l'accès est donc protégé par un verrou (voir console_log.c). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define CONSOLE_LIGNES_MAX 24
#define CONSOLE_LIGNE_MAX  96

/* Instantané complet du scrollback : un tampon circulaire de
 * `CONSOLE_LIGNES_MAX` lignes, `debut` désignant l'index de la plus ANCIENNE
 * ligne valide (les lignes suivantes, jusqu'à `nb`, s'enchaînent modulo
 * `CONSOLE_LIGNES_MAX`). Au-delà de `nb` lignes, le contenu de `lignes[]`
 * n'a aucune signification -- même convention que `klipper_fichiers_t`/
 * `power_devices_t`. */
typedef struct {
    char     lignes[CONSOLE_LIGNES_MAX][CONSOLE_LIGNE_MAX];
    uint8_t  debut;      /* index de la plus ancienne ligne valide (ring) */
    uint8_t  nb;         /* nb de lignes valides (<= CONSOLE_LIGNES_MAX) */
    uint32_t generation; /* incrémentée à chaque écriture (ajouter()/
                          * effacer()) -- permet à l'UI de détecter qu'il y a
                          * du neuf sans comparer le contenu ligne par ligne,
                          * même contrat que power_devices_t.generation */
} console_log_t;

/* Ajoute UNE ligne en fin de scrollback (sous verrou), en tronquant à
 * `CONSOLE_LIGNE_MAX - 1` caractères si nécessaire (troncature SILENCIEUSE,
 * jamais de dépassement -- même politique que les autres champs texte bornés
 * de ce dépôt, ex. `klipper_fichiers_t.noms[]`). Si le store est déjà plein
 * (`nb == CONSOLE_LIGNES_MAX`), la plus ancienne ligne est ÉVINCÉE (FIFO,
 * `debut` avance d'un cran) pour faire de la place -- ce n'est PAS une
 * troncature de compte comme `klipper_fichiers_t.tronques` : le scrollback
 * n'a pas vocation à conserver un historique complet, seulement les
 * dernières lignes. `generation` est incrémentée dans tous les cas.
 *
 * Cette fonction ne découpe PAS elle-même les réponses Klipper multi-lignes
 * (séparées par `\n`) : c'est à l'appelant (moonraker_ws.c, tâche B) d'
 * appeler `console_log_ajouter()` une fois par ligne déjà séparée -- garde
 * cette fonction simple et son contrat symétrique avec l'écho local d'une
 * commande utilisateur (toujours une seule ligne).
 *
 * `ligne` NULL = no-op (ne touche ni le store ni `generation`). */
void console_log_ajouter(const char *ligne);

/* Copie le contenu courant du store dans `*dest` (fourni par l'appelant, sous
 * verrou). `dest` NULL = no-op. */
void console_log_lire(console_log_t *dest);

/* Vide ENTIÈREMENT le scrollback (`nb = 0`, `debut = 0`) et incrémente
 * `generation`, sous verrou. Le contenu de `lignes[]` n'est pas remis à
 * zéro (sans signification au-delà de `nb == 0`, même convention que
 * `console_log_ajouter()` ci-dessus) -- seul le compte change. */
void console_log_effacer(void);
