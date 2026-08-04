/* Implémentation de usb_upload.h -- voir ce header pour le contrat complet et
 * le POURQUOI (cadrage multipart pur, tâche A de la feature "Impression
 * depuis USB"). */
#include "usb_upload.h"

#include <stdio.h>
#include <string.h>
#include <strings.h> /* strcasecmp -- déjà utilisé dans ce BSP, voir
                       * examples/display_slideshow.c:scan_usb_for_pngs() */

bool usb_est_gcode(const char *nom)
{
    static const char *const EXTENSIONS[] = { ".gcode", ".gco", ".g" };

    if (nom == NULL) {
        return false;
    }

    size_t len = strlen(nom);
    for (size_t i = 0; i < sizeof(EXTENSIONS) / sizeof(EXTENSIONS[0]); i++) {
        size_t elen = strlen(EXTENSIONS[i]);
        if (len >= elen && strcasecmp(nom + (len - elen), EXTENSIONS[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Taille max du filename NETTOYÉ retenue pour composer le préambule : les
 * noms longs (LFN FAT32) plafonnent à 255 caractères, largement couvert.
 * Un nom plus long que ça (cas d'école, hors de tout système de fichiers FAT
 * réel) serait tronqué ICI, silencieusement, avant même d'atteindre le
 * contrat retour façon snprintf() de usb_upload_preambule() -- documenté
 * plutôt que traité comme un cas à part, vu qu'aucune source réelle de
 * `filename` dans ce dépôt (listing USB, voir la spec) ne peut produire un
 * nom aussi long. */
#define USB_UPLOAD_FILENAME_MAX 256

/* Retire tout '"'/'\r'/'\n' de `filename` -- défense injection d'en-tête
 * HTTP (voir usb_upload_preambule() dans le header). Même style que
 * json_echapper_chaine() (json_util.c) : contrat retour façon snprintf(),
 * `dest`/`dest_n` peuvent être NULL/0 pour sonder la longueur, `filename ==
 * NULL` traité comme une chaîne vide. Pas de "groupe atomique" à préserver
 * ici (contrairement à json_echapper_chaine()) : chaque caractère produit
 * au plus 1 octet de sortie -- soit recopié tel quel, soit retiré -- jamais
 * une séquence multi-octets qui pourrait être coupée en deux. */
static size_t nettoyer_filename(char *dest, size_t dest_n, const char *filename)
{
    size_t pos = 0;
    size_t ecrit = 0;
    const int a_de_la_place = (dest != NULL && dest_n > 0);

    if (filename != NULL) {
        for (const unsigned char *p = (const unsigned char *)filename; *p != '\0'; p++) {
            unsigned char c = *p;
            if (c == '"' || c == '\r' || c == '\n') {
                continue; /* retiré, jamais recopié */
            }
            if (a_de_la_place && pos < dest_n - 1) {
                dest[pos++] = (char)c;
            }
            ecrit++;
        }
    }

    if (a_de_la_place) {
        dest[pos] = '\0';
    }
    return ecrit;
}

/* Gabarit unique du préambule, partagé par les deux chemins (sonde/écriture)
 * de usb_upload_preambule() ci-dessous -- une seule source de vérité pour le
 * format exact du multipart, voir le commentaire de usb_upload_preambule()
 * dans le header pour le rendu attendu. */
#define USB_UPLOAD_PREAMBULE_FMT                                             \
    "--%s\r\n"                                                              \
    "Content-Disposition: form-data; name=\"root\"\r\n"                     \
    "\r\n"                                                                  \
    "gcodes\r\n"                                                            \
    "--%s\r\n"                                                              \
    "Content-Disposition: form-data; name=\"print\"\r\n"                    \
    "\r\n"                                                                  \
    "true\r\n"                                                              \
    "--%s\r\n"                                                              \
    "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"    \
    "Content-Type: application/octet-stream\r\n"                           \
    "\r\n"

size_t usb_upload_preambule(char *dest, size_t n, const char *boundary,
                            const char *filename)
{
    char nom_propre[USB_UPLOAD_FILENAME_MAX];
    nettoyer_filename(nom_propre, sizeof(nom_propre), filename);

    const char *b = (boundary != NULL) ? boundary : "";

    /* dest/n NULL/0 : sonde de longueur seule, jamais d'écriture -- même
     * usage que snprintf(NULL, 0, ...), explicitement gardé (plutôt que de
     * compter sur le comportement de la libc pour dest==NULL avec n>0, qui
     * n'est PAS défini par le standard). */
    if (dest == NULL || n == 0) {
        int besoin = snprintf(NULL, 0, USB_UPLOAD_PREAMBULE_FMT, b, b, b, nom_propre);
        return (besoin > 0) ? (size_t)besoin : 0;
    }

    int ecrit = snprintf(dest, n, USB_UPLOAD_PREAMBULE_FMT, b, b, b, nom_propre);
    return (ecrit > 0) ? (size_t)ecrit : 0;
}

size_t usb_upload_trailer(char *dest, size_t n, const char *boundary)
{
    const char *b = (boundary != NULL) ? boundary : "";

    if (dest == NULL || n == 0) {
        int besoin = snprintf(NULL, 0, "\r\n--%s--\r\n", b);
        return (besoin > 0) ? (size_t)besoin : 0;
    }

    int ecrit = snprintf(dest, n, "\r\n--%s--\r\n", b);
    return (ecrit > 0) ? (size_t)ecrit : 0;
}

size_t usb_upload_content_length(size_t preambule_len, size_t taille_fichier,
                                 size_t trailer_len)
{
    return preambule_len + taille_fichier + trailer_len;
}
