/* Démarrage paresseux du sous-système USB (feature "Impression depuis USB",
 * tâche B, devenue explorateur -- spec 2026-08-14-usb-explorateur-design.md) :
 * montage MSC (pandatouch_msc.h) + callbacks mount/unmount + tâche de
 * LISTAGE à la demande, qui remplit le store dédié usb_fichiers.h avec le
 * contenu d'UN répertoire (plus jamais de scan récursif complet).
 *
 * POURQUOI paresseux (fix RAM interne, voir la mémoire du projet) : ce code
 * vivait dans app_main.c et démarrait AU BOOT, juste après web_set_boot_count().
 * pt_usb_start() y créait alors 2 tâches hôte USB (~8 Ko de RAM INTERNE --
 * les piles de tâche restent TOUJOURS en RAM interne, jamais en PSRAM) au
 * moment précis où la tâche WebSocket (16 Ko, elle aussi RAM interne)
 * cherchait à s'allouer -- "Error create websocket task". Ce sous-système ne
 * démarre plus qu'à la PREMIÈRE ouverture de l'écran USB (ecran_usb.c,
 * construire()) : app_main.c ne l'appelle plus au boot. Bonus documenté par
 * cette même tâche : supprime aussi le risque de reboot par
 * l'ESP_ERROR_CHECK interne de pt_usb_start() (voir son ancien commentaire
 * dans app_main.c) survenant désormais BIEN après que WiFi/serveur HTTP/écran
 * soient déjà debout, au lieu de tout premier étage du boot.
 *
 * ESP-only en interne (pandatouch_msc.h + FreeRTOS), mais cette fonction est
 * appelable INCONDITIONNELLEMENT -- y compris depuis host-test, où
 * ecran_usb.c est compilé pour un motif de non-régression (voir son
 * commentaire de tête : esp_http_client n'existe pas côté host-test, un mock
 * le remplace, mais ecran_usb.c lui-même doit rester compilable tel quel) :
 * hors ESP_PLATFORM, cette fonction est un no-op, même politique que le
 * verrou de klipper_fichiers.c. Idempotente (flag "déjà démarré" statique,
 * SAUF en cas d'échec d'allocation PSRAM -- voir usb_scan.c) : rappelable à
 * chaque construire()/mettre_a_jour() de ecran_usb.c sans effet après le
 * premier appel réel. */
#pragma once

void usb_scan_demarrage_paresseux(void);

/* Demande le listage du répertoire `chemin` (chemin complet sous /usb ;
 * NULL ou vide => racine "/usb"). Asynchrone : la tâche de listage publie le
 * résultat dans le store usb_fichiers.h (génération incrémentée) dès que
 * possible. Deux demandes rapprochées : la DERNIÈRE gagne (tampon écrasé,
 * sémaphore coalescent) -- c'est le comportement voulu pour des taps de
 * navigation successifs. No-op hors ESP_PLATFORM (même politique que
 * usb_scan_demarrage_paresseux() ci-dessus). */
void usb_scan_demander(const char *chemin);
