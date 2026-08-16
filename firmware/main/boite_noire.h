/* Boîte noire RTC : un mot de drapeaux en mémoire RTC_NOINIT, survivant à
 * tout reset logiciel/watchdog (comme le compteur de rescue.c), levé à
 * l'ENTRÉE d'une zone suspecte et rabattu à sa sortie -- lu et journalisé au
 * boot suivant par app_main. POURQUOI (chasse aux WDT(7) muets du
 * 2026-08-15) : ces blocages coupent les interruptions des deux cœurs, le
 * coredump ne peut jamais s'écrire -- la seule trace possible est un
 * marqueur posé AVANT le drame. Un boot propre lit 0 ; un boot après crash
 * lit les zones qui étaient actives à l'instant du blocage (corrélation,
 * pas preuve -- mais après 5 crashs sans aucune donnée, une corrélation
 * fiable vaut de l'or).
 *
 * Chaque bit = une zone (voir BOITE_NOIRE_*). Host : no-op complet, ce
 * fichier reste incluable partout (clavier.c est host-compilé). */
#pragma once

#include <stdint.h>

#define BOITE_NOIRE_SONDE_HTTP (1u << 0) /* parc_sonde : requête HTTP en vol */
#define BOITE_NOIRE_CLAVIER    (1u << 1) /* clavier plein écran ouvert */
#define BOITE_NOIRE_WS_RX      (1u << 2) /* moonraker_ws : traitement d'un message reçu */

#ifdef ESP_PLATFORM
void boite_noire_lever(uint32_t bit);
void boite_noire_rabattre(uint32_t bit);
/* Mot au moment du DERNIER reset (capturé puis remis à zéro par le premier
 * appel, à faire tôt dans app_main). */
uint32_t boite_noire_relever(void);
#else
static inline void boite_noire_lever(uint32_t bit) { (void)bit; }
static inline void boite_noire_rabattre(uint32_t bit) { (void)bit; }
static inline uint32_t boite_noire_relever(void) { return 0; }
#endif
