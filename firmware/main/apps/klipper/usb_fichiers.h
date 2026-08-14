/* Store dédié de l'état de la clé USB (montée/absente) et des .gcode qui y
 * ont été trouvés par le scan de démarrage paresseux (voir usb_scan.h,
 * callbacks pt_usb_on_mount()/pt_usb_on_unmount()).
 *
 * POURQUOI un store séparé, HORS etat_klipper_t (même choix, même raison que
 * klipper_fichiers.h, voir son commentaire de tête complet) : etat_klipper_t
 * est un POD copié PARTOUT (copies statiques + posé sur des piles de tâches),
 * y ajouter une liste de fichiers USB (jusqu'à USB_FICHIERS_MAX x
 * USB_FICHIER_CHEMIN_MAX, ~4 Ko) reproduirait exactement l'épuisement de RAM
 * interne qui avait empêché la tâche WebSocket de s'allouer avant que
 * klipper_fichiers.h n'en soit sorti. Ce store lui-même vit désormais EN
 * PSRAM plutôt qu'en .bss RAM interne (même fix, voir usb_fichiers.c). Il est
 * écrit par la tâche de scan dédiée créée au montage (usb_scan.c, jamais sur
 * la pile 4 Ko des tâches USB du BSP -- voir leur commentaire) et lu par
 * l'écran USB (tâche LVGL) : même politique de verrou COURT que
 * klipper_fichiers.c.
 *
 * `taille` (pt_usb_dir_entry_t.size, voir pandatouch_msc.h) est propagée ICI
 * depuis le scan pour que usb_upload_http.c puisse calculer le
 * Content-Length de l'upload SANS avoir à relister la clé au moment du tap
 * utilisateur (l'utilisateur peut taper longtemps après le scan). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Nombre maximal d'entrées retenues PAR RÉPERTOIRE (explorateur, spec
 * 2026-08-14-usb-explorateur-design.md : le store ne porte plus un scan
 * complet de la clé mais le contenu du répertoire courant -- sous-dossiers
 * + .gcode). 64 entrées x ~136 o ≈ 9 Ko, en PSRAM (voir usb_fichiers.c).
 * Au-delà, `tronques` (voir plus bas) le signale. */
#define USB_FICHIERS_MAX 64

/* Racine du montage USB. Côté ESP : ALIAS de PT_USB_MOUNT_PATH (BSP) -- une
 * divergence est structurellement impossible (revue du 2026-08-15, L6 :
 * la version précédente dupliquait la chaîne avec des gardes, dont l'échec
 * aurait silencieusement désactivé l'USB). Côté host/simulateur, où le BSP
 * n'existe pas : la valeur en dur, même politique d'#ifdef que journal.h. */
#ifdef ESP_PLATFORM
#include "pandatouch_msc.h"
#define USB_RACINE PT_USB_MOUNT_PATH
#else
#define USB_RACINE "/usb"
#endif

/* Longueur max d'un chemin complet SOUS /usb (ex. "/usb/sous-dossier/
 * piece.gcode"), NUL compris. Plus large que KLIPPER_FICHIER_MAX (64, un nom
 * relatif à la racine "gcodes" de Moonraker) : un chemin USB porte en plus
 * le préfixe "/usb/" et peut descendre plus profond en sous-dossiers -- 128
 * couvre largement un nom long FAT32 (LFN, 255 caractères max) tronqué au
 * besoin (voir usb_scan_recursif(), usb_scan.c, qui borne défensivement
 * plutôt que déborder). */
#define USB_FICHIER_CHEMIN_MAX 128

typedef struct {
    char   chemin[USB_FICHIER_CHEMIN_MAX]; /* chemin complet, ex "/usb/piece.gcode" */
    size_t taille;                         /* octets (0 pour un dossier) */
    bool   est_dossier;                    /* vrai = navigable, faux = .gcode imprimable */
} usb_fichier_t;

typedef struct {
    bool          monte;                        /* clé USB actuellement montée ? */
    /* Répertoire dont `fichiers` est le contenu ("" clé absente) -- affiché
     * par l'écran dans sa rangée de statut, et base des chemins parents
     * (entrée ".." injectée par l'écran, jamais par ce store). */
    char          chemin_courant[USB_FICHIER_CHEMIN_MAX];
    usb_fichier_t fichiers[USB_FICHIERS_MAX];
    uint8_t       nb;                            /* entrées valides dans fichiers[0..nb-1] */
    bool          tronques;                      /* vrai si le répertoire en contenait davantage */
    /* Vrai entre usb_fichiers_scan_demarre() (demande de listage, voir
     * usb_scan_demander()) et le usb_fichiers_definir() qui publie le
     * résultat. POURQUOI (héritée du fix "Insert a USB key" mensonger,
     * diagnostic du 2026-08-14) : sans état intermédiaire, l'écran mentait
     * pendant la lecture de la clé. Un listage d'UN répertoire est court
     * (contrairement au scan complet d'alors), mais un gros dossier sur une
     * clé lente peut prendre 1-2 s : l'état honnête reste requis. */
    bool          scan_en_cours;
} usb_fichiers_t;

/* Remplace ENTIÈREMENT le contenu du store (copie sous verrou).
 * `chemin_courant` est le répertoire listé (copié borné ; NULL ou clé
 * absente => ""). `fichiers` peut être NULL si `nb` vaut 0 (cas du unmount :
 * usb_fichiers_definir(false, "", NULL, 0, false)). `nb` est borné
 * défensivement à USB_FICHIERS_MAX ici même si l'appelant a déjà normalement
 * respecté cette borne (même discipline que klipper_fichiers_definir()).
 * Incrémente le compteur de génération interne (voir
 * usb_fichiers_generation() ci-dessous) -- TOUJOURS, même si rien n'a
 * changé : un appelant qui rappelle cette fonction a une raison de le faire
 * (un nouveau listage a tourné), l'écran doit pouvoir le détecter. Clôt
 * aussi `scan_en_cours` (toute publication termine le listage). */
void usb_fichiers_definir(bool monte, const char *chemin_courant,
                          const usb_fichier_t *fichiers, uint8_t nb, bool tronques);

/* Lève `scan_en_cours` (et incrémente la génération, pour que l'écran le
 * voie) -- à appeler quand un scan démarre RÉELLEMENT (clé montée, tâche de
 * scan réveillée), jamais en simple intention. Retombe au prochain
 * usb_fichiers_definir(), qu'il vienne du scan (résultat publié) ou du
 * unmount (éjection pendant le scan : "clé absente" ne doit jamais cohabiter
 * avec "lecture en cours"). */
void usb_fichiers_scan_demarre(void);

/* Dernier segment d'un chemin ("/usb/dossier/piece.gcode" -> "piece.gcode" ;
 * pas de '/' -> le chemin entier ; NULL -> ""). Fonction PURE, exposée pour
 * l'écran (libellés = nom seul) et les tests host. */
const char *usb_chemin_nom(const char *chemin);

/* Chemin PARENT de `chemin`, borné, plancher USB_RACINE ("/usb/a/b" ->
 * "/usb/a" ; "/usb/a" -> "/usb" ; "/usb", NULL, chemin hors racine ou trop
 * court -> USB_RACINE). Fonction PURE, testée host -- c'est elle que l'écran
 * met derrière l'entrée ".." (revue du 2026-08-15, L9 : la version inline
 * n'était exercée qu'au tap réel sur la dalle). `dest`/`n` façon strlcpy :
 * toujours NUL-terminé si n > 0. */
void usb_chemin_parent(char *dest, size_t n, const char *chemin);

/* Trie `entrees[0..nb-1]` en place : dossiers d'abord, puis ordre
 * alphabétique du NOM (dernier segment), insensible à la casse
 * (strcasecmp) -- l'ordre FAT brut est arbitraire, inutilisable tel quel
 * pour naviguer. Fonction PURE (qsort), testée host ; NULL/nb<2 = no-op. */
void usb_listing_trier(usb_fichier_t *entrees, uint8_t nb);

/* Copie le contenu courant du store dans `*dest` (fourni par l'appelant,
 * sous verrou). `dest` NULL = no-op. */
void usb_fichiers_lire(usb_fichiers_t *dest);

/* Compteur monotone, +1 à chaque usb_fichiers_definir() -- même idiome que
 * klipper_temp_historique_generation() : un consommateur (ecran_usb.c) ne
 * redessine sa liste que quand cette valeur a changé, plutôt qu'à chaque
 * mettre_a_jour() (potentiellement plusieurs fois par seconde). */
uint32_t usb_fichiers_generation(void);
