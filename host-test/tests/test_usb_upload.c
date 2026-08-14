/* Tâche A (feature "Impression depuis USB") : cadrage multipart pur de
 * l'upload vers Moonraker (usb_upload.h) -- filtre d'extension, préambule/
 * trailer multipart, nettoyage du filename (défense injection d'en-tête),
 * Content-Length. Fonctions pures sans état, aucune contrainte d'ordre avec
 * les autres suites. */
#include <string.h>

#include "petit_test.h"
#include "usb_fichiers.h"
#include "usb_upload.h"

/* --- usb_est_gcode ------------------------------------------------------ */

static void section_est_gcode(void)
{
    VERIFIER(usb_est_gcode("piece.gcode"));
    VERIFIER(usb_est_gcode("PIECE.GCODE"));
    VERIFIER(usb_est_gcode("piece.Gco"));
    VERIFIER(usb_est_gcode("piece.gco"));
    VERIFIER(usb_est_gcode("piece.g"));
    VERIFIER(usb_est_gcode("piece.G"));
    VERIFIER(usb_est_gcode("sous/dossier/piece.gcode"));
    VERIFIER(usb_est_gcode(".gcode")); /* extension seule : "se termine par" */

    VERIFIER(!usb_est_gcode("photo.png"));
    VERIFIER(!usb_est_gcode("notes.txt"));
    VERIFIER(!usb_est_gcode("sansextension"));
    VERIFIER(!usb_est_gcode("piece.gcodee")); /* pas EXACTEMENT l'extension */
    VERIFIER(!usb_est_gcode(""));
    VERIFIER(!usb_est_gcode(NULL));
}

/* --- usb_upload_preambule ------------------------------------------------ */

#define PREAMBULE_ATTENDU                                                          \
    "--TESTBOUND\r\n"                                                             \
    "Content-Disposition: form-data; name=\"root\"\r\n"                          \
    "\r\n"                                                                        \
    "gcodes\r\n"                                                                  \
    "--TESTBOUND\r\n"                                                             \
    "Content-Disposition: form-data; name=\"print\"\r\n"                         \
    "\r\n"                                                                        \
    "true\r\n"                                                                    \
    "--TESTBOUND\r\n"                                                             \
    "Content-Disposition: form-data; name=\"file\"; filename=\"piece.gcode\"\r\n" \
    "Content-Type: application/octet-stream\r\n"                                 \
    "\r\n"

static void section_preambule_nominal(void)
{
    char dest[512];
    size_t n = usb_upload_preambule(dest, sizeof(dest), "TESTBOUND", "piece.gcode");
    VERIFIER_TEXTE(dest, PREAMBULE_ATTENDU);
    VERIFIER(n == strlen(PREAMBULE_ATTENDU));
    VERIFIER(n == strlen(dest));

    /* les trois parts sont bien présentes, DANS L'ORDRE root -> print -> file */
    char *p_root = strstr(dest, "name=\"root\"");
    char *p_print = strstr(dest, "name=\"print\"");
    char *p_file = strstr(dest, "name=\"file\"");
    VERIFIER(p_root != NULL && p_print != NULL && p_file != NULL);
    VERIFIER(p_root < p_print);
    VERIFIER(p_print < p_file);

    VERIFIER(strstr(dest, "gcodes\r\n") != NULL);
    VERIFIER(strstr(dest, "true\r\n") != NULL);
    VERIFIER(strstr(dest, "Content-Type: application/octet-stream\r\n") != NULL);
}

static void section_preambule_sonde(void)
{
    /* dest == NULL : sonde de longueur, comme snprintf(NULL, 0, ...) */
    size_t n = usb_upload_preambule(NULL, 0, "TESTBOUND", "piece.gcode");
    VERIFIER(n == strlen(PREAMBULE_ATTENDU));

    /* n == 0 avec dest non-NULL : même sonde, rien n'est écrit */
    char dest[8] = "bruit";
    n = usb_upload_preambule(dest, 0, "TESTBOUND", "piece.gcode");
    VERIFIER(n == strlen(PREAMBULE_ATTENDU));
    VERIFIER_TEXTE(dest, "bruit"); /* intact : aucune écriture */
}

static void section_preambule_troncature(void)
{
    char petit[20];
    size_t n = usb_upload_preambule(petit, sizeof(petit), "TESTBOUND", "piece.gcode");

    VERIFIER(n == strlen(PREAMBULE_ATTENDU)); /* longueur réelle, pas celle du tampon */
    VERIFIER(n >= sizeof(petit));             /* signale la troncature */
    VERIFIER(strlen(petit) == sizeof(petit) - 1);
    VERIFIER(memcmp(petit, PREAMBULE_ATTENDU, sizeof(petit) - 1) == 0); /* préfixe valide */
    VERIFIER(petit[sizeof(petit) - 1] == '\0');
}

static void section_preambule_filename_dangereux(void)
{
    char dest[512];
    /* guillemet + CRLF injectés dans le nom -- doivent être RETIRÉS (pas
     * échappés) pour ne jamais casser ni étendre les en-têtes HTTP autour. */
    size_t n = usb_upload_preambule(dest, sizeof(dest), "TESTBOUND", "a\"b\r\nc.gcode");
    VERIFIER(n == strlen(dest));
    /* le filename nettoyé ("abc.gcode") est directement suivi de la ligne
     * Content-Type attendue -- aucun octet parasite injecté entre les deux. */
    VERIFIER(strstr(dest, "filename=\"abc.gcode\"\r\nContent-Type: application/octet-stream\r\n") != NULL);
    VERIFIER(strstr(dest, "\"a\"b\"") == NULL);
}

static void section_preambule_null_defaults(void)
{
    char dest[512];
    /* boundary/filename NULL -> traités comme chaîne vide, jamais de crash */
    size_t n = usb_upload_preambule(dest, sizeof(dest), NULL, NULL);
    VERIFIER(n == strlen(dest));
    VERIFIER(strstr(dest, "--\r\nContent-Disposition: form-data; name=\"root\"") != NULL);
    VERIFIER(strstr(dest, "filename=\"\"") != NULL);
}

/* --- usb_upload_trailer -------------------------------------------------- */

static void section_trailer(void)
{
    char dest[64];
    size_t n = usb_upload_trailer(dest, sizeof(dest), "TESTBOUND");
    VERIFIER_TEXTE(dest, "\r\n--TESTBOUND--\r\n");
    VERIFIER(n == strlen(dest));

    /* sonde */
    n = usb_upload_trailer(NULL, 0, "TESTBOUND");
    VERIFIER(n == strlen("\r\n--TESTBOUND--\r\n"));

    /* troncature */
    char petit[5];
    n = usb_upload_trailer(petit, sizeof(petit), "TESTBOUND");
    VERIFIER(n == strlen("\r\n--TESTBOUND--\r\n"));
    VERIFIER(strlen(petit) == sizeof(petit) - 1);

    /* boundary NULL -> chaîne vide */
    n = usb_upload_trailer(dest, sizeof(dest), NULL);
    VERIFIER_TEXTE(dest, "\r\n----\r\n");
    VERIFIER(n == strlen(dest));
}

/* --- usb_upload_content_length ------------------------------------------- */

static void section_content_length(void)
{
    VERIFIER(usb_upload_content_length(0, 0, 0) == 0);
    VERIFIER(usb_upload_content_length(120, 45000, 19) == 120 + 45000 + 19);
    VERIFIER(usb_upload_content_length(strlen(PREAMBULE_ATTENDU), 1000000,
                                        strlen("\r\n--TESTBOUND--\r\n"))
              == strlen(PREAMBULE_ATTENDU) + 1000000 + strlen("\r\n--TESTBOUND--\r\n"));
}

/* --- store usb_fichiers : repertoire courant (explorateur) ---------------- */

/* Explorateur de fichiers USB (spec 2026-08-14-usb-explorateur-design.md) :
 * le store ne porte plus le resultat d'un scan complet mais l'etat du
 * REPERTOIRE COURANT -- chemin + entrees (dossiers et .gcode). Contrat :
 * scan_demarre() leve le drapeau de listage (+1 generation) ; definir()
 * publie chemin + entrees et clot le listage (unmount compris). */
static void section_store_repertoire(void)
{
    usb_fichiers_t lu;

    usb_fichiers_definir(false, "", NULL, 0, false);
    usb_fichiers_lire(&lu);
    VERIFIER(!lu.monte);
    VERIFIER(lu.chemin_courant[0] == '\0');

    uint32_t generation_avant = usb_fichiers_generation();
    usb_fichiers_scan_demarre();
    usb_fichiers_lire(&lu);
    VERIFIER(lu.scan_en_cours);
    VERIFIER(usb_fichiers_generation() == generation_avant + 1);

    usb_fichier_t entrees[2];
    memset(entrees, 0, sizeof(entrees));
    strcpy(entrees[0].chemin, "/usb/dossier");
    entrees[0].est_dossier = true;
    strcpy(entrees[1].chemin, "/usb/piece.gcode");
    entrees[1].taille = 1234;
    usb_fichiers_definir(true, "/usb", entrees, 2, false);
    usb_fichiers_lire(&lu);
    VERIFIER(lu.monte);
    VERIFIER(!lu.scan_en_cours);
    VERIFIER_TEXTE(lu.chemin_courant, "/usb");
    VERIFIER(lu.nb == 2);
    VERIFIER(lu.fichiers[0].est_dossier);
    VERIFIER(!lu.fichiers[1].est_dossier);
    VERIFIER(lu.fichiers[1].taille == 1234);

    /* Invariant PORTANT (revue du 2026-08-15, L7) : scan_demarre() ne touche
       QUE le drapeau + la generation -- monte/nb/chemin restent intacts.
       C'est ce qui permet a l'ecran de continuer d'afficher le repertoire
       courant pendant qu'une navigation se liste ; un scan_demarre() qui
       viderait le store blanchirait la grille a chaque tap. */
    usb_fichiers_scan_demarre();
    usb_fichiers_lire(&lu);
    VERIFIER(lu.scan_en_cours);
    VERIFIER(lu.monte);
    VERIFIER(lu.nb == 2);
    VERIFIER_TEXTE(lu.chemin_courant, "/usb");
    VERIFIER_TEXTE(lu.fichiers[1].chemin, "/usb/piece.gcode");

    /* Ejection : tout retombe, chemin compris -- jamais un chemin fantome
     * d'une cle retiree. */
    usb_fichiers_scan_demarre();
    usb_fichiers_definir(false, "", NULL, 0, false);
    usb_fichiers_lire(&lu);
    VERIFIER(!lu.scan_en_cours);
    VERIFIER(!lu.monte);
    VERIFIER(lu.nb == 0);
    VERIFIER(lu.chemin_courant[0] == '\0');
}

/* --- tri du listing (dossiers d'abord, alphabetique) ---------------------- */

static void section_listing_tri(void)
{
    VERIFIER_TEXTE(usb_chemin_nom("/usb/dossier/piece.gcode"), "piece.gcode");
    VERIFIER_TEXTE(usb_chemin_nom("sans-slash"), "sans-slash");
    VERIFIER_TEXTE(usb_chemin_nom(NULL), "");

    usb_fichier_t e[4];
    memset(e, 0, sizeof(e));
    strcpy(e[0].chemin, "/usb/zeta.gcode");
    strcpy(e[1].chemin, "/usb/Beta");
    e[1].est_dossier = true;
    strcpy(e[2].chemin, "/usb/alpha.gcode");
    strcpy(e[3].chemin, "/usb/gamma");
    e[3].est_dossier = true;
    usb_listing_trier(e, 4);
    /* dossiers d'abord (alpha, insensible a la casse), puis fichiers alpha */
    VERIFIER_TEXTE(usb_chemin_nom(e[0].chemin), "Beta");
    VERIFIER_TEXTE(usb_chemin_nom(e[1].chemin), "gamma");
    VERIFIER_TEXTE(usb_chemin_nom(e[2].chemin), "alpha.gcode");
    VERIFIER_TEXTE(usb_chemin_nom(e[3].chemin), "zeta.gcode");

    usb_listing_trier(NULL, 0); /* garde NULL : ne crashe pas */
}

/* --- chemin parent (entree "..") ------------------------------------------ */

static void section_chemin_parent(void)
{
    char parent[USB_FICHIER_CHEMIN_MAX];

    usb_chemin_parent(parent, sizeof(parent), "/usb/a/b");
    VERIFIER_TEXTE(parent, "/usb/a");
    usb_chemin_parent(parent, sizeof(parent), "/usb/a");
    VERIFIER_TEXTE(parent, "/usb");
    /* Deja a la racine, chemin invalide ou hors racine : plancher /usb. */
    usb_chemin_parent(parent, sizeof(parent), "/usb");
    VERIFIER_TEXTE(parent, "/usb");
    usb_chemin_parent(parent, sizeof(parent), NULL);
    VERIFIER_TEXTE(parent, "/usb");
    usb_chemin_parent(parent, sizeof(parent), "/ailleurs/x");
    VERIFIER_TEXTE(parent, "/usb");
    usb_chemin_parent(parent, sizeof(parent), "");
    VERIFIER_TEXTE(parent, "/usb");
    /* dest minuscule : toujours NUL-terminé, jamais de débordement. */
    char mini[3];
    usb_chemin_parent(mini, sizeof(mini), "/usb/a/b");
    VERIFIER(strlen(mini) == 2);
    usb_chemin_parent(NULL, 0, "/usb/a"); /* gardes : ne crashe pas */
}

void suite_usb_upload(void)
{
    printf("suite : usb_upload (cadrage multipart upload USB->Moonraker)\n");
    section_est_gcode();
    section_preambule_nominal();
    section_preambule_sonde();
    section_preambule_troncature();
    section_preambule_filename_dangereux();
    section_preambule_null_defaults();
    section_trailer();
    section_content_length();
    section_store_repertoire();
    section_listing_tri();
    section_chemin_parent();
}
