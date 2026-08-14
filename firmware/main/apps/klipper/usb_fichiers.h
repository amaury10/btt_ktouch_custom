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

/* Nombre maximal de fichiers .gcode retenus par le scan -- même borne que
 * KLIPPER_FICHIERS_MAX (etat_klipper.h), pas réutilisée directement : ce
 * store est délibérément indépendant de tout ce qui vient de Moonraker
 * (source, contrat de nommage différents -- des chemins USB, pas des noms
 * connus de Moonraker). Au-delà, `tronques` (voir plus bas) le signale. */
#define USB_FICHIERS_MAX 32

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
    size_t taille;                         /* octets, depuis pt_usb_dir_entry_t.size */
} usb_fichier_t;

typedef struct {
    bool          monte;                        /* clé USB actuellement montée ? */
    usb_fichier_t fichiers[USB_FICHIERS_MAX];
    uint8_t       nb;                            /* fichiers valides dans fichiers[0..nb-1] */
    bool          tronques;                      /* vrai si la clé en contenait davantage */
    /* Vrai entre usb_fichiers_scan_demarre() (callback de montage) et le
     * usb_fichiers_definir() qui publie le résultat du scan. POURQUOI (fix
     * "Insert a USB key" mensonger, diagnostic du 2026-08-14) : pendant tout
     * le scan (45 s observées sur une clé réelle bien remplie), `monte`
     * reste faux -- le résultat n'est publié qu'à la fin -- et l'écran
     * affichait "Insert a USB key" clé branchée et montée. Ce drapeau donne
     * à l'écran l'état intermédiaire honnête ("lecture de la clé..."). */
    bool          scan_en_cours;
} usb_fichiers_t;

/* Remplace ENTIÈREMENT le contenu du store (copie sous verrou). `fichiers`
 * peut être NULL si `nb` vaut 0 (cas du unmount : usb_fichiers_definir(false,
 * NULL, 0, false)). `nb` est borné défensivement à USB_FICHIERS_MAX ici même
 * si l'appelant a déjà normalement respecté cette borne (même discipline que
 * klipper_fichiers_definir()). Incrémente le compteur de génération interne
 * (voir usb_fichiers_generation() ci-dessous) -- TOUJOURS, même si `monte`
 * et le contenu n'ont pas changé : un appelant qui rappelle cette fonction a
 * une raison de le faire (un nouveau scan a tourné), l'écran doit pouvoir le
 * détecter même si le résultat est identique au précédent. */
void usb_fichiers_definir(bool monte, const usb_fichier_t *fichiers, uint8_t nb, bool tronques);

/* Lève `scan_en_cours` (et incrémente la génération, pour que l'écran le
 * voie) -- à appeler quand un scan démarre RÉELLEMENT (clé montée, tâche de
 * scan réveillée), jamais en simple intention. Retombe au prochain
 * usb_fichiers_definir(), qu'il vienne du scan (résultat publié) ou du
 * unmount (éjection pendant le scan : "clé absente" ne doit jamais cohabiter
 * avec "lecture en cours"). */
void usb_fichiers_scan_demarre(void);

/* Publication PARTIELLE pendant un scan (fix lenteur perçue du 2026-08-14 :
 * un parcours complet de clé bien remplie prend des dizaines de secondes ;
 * les .gcode déjà trouvés doivent s'afficher au fil de l'eau, pas à la fin).
 * Même copie sous verrou que usb_fichiers_definir() avec monte=true, MAIS
 * `scan_en_cours` reste levé : seul definir() (publication finale, ou
 * unmount) clôt le scan. À n'appeler QUE depuis la tâche de scan, clé
 * montée. */
void usb_fichiers_publier_partiel(const usb_fichier_t *fichiers, uint8_t nb, bool tronques);

/* Copie le contenu courant du store dans `*dest` (fourni par l'appelant,
 * sous verrou). `dest` NULL = no-op. */
void usb_fichiers_lire(usb_fichiers_t *dest);

/* Compteur monotone, +1 à chaque usb_fichiers_definir() -- même idiome que
 * klipper_temp_historique_generation() : un consommateur (ecran_usb.c) ne
 * redessine sa liste que quand cette valeur a changé, plutôt qu'à chaque
 * mettre_a_jour() (potentiellement plusieurs fois par seconde). */
uint32_t usb_fichiers_generation(void);
