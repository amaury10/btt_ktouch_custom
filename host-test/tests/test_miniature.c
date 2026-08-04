/* Feature "Miniatures gcode", tâche B (intégration ESP) : tests hôte du
 * store dédié (miniature.h/.c, portable -- voir son commentaire de tête pour
 * le mécanisme de transfert différé producteur/consommateur) ET, en
 * de-risking recommandé par le brief de la tâche, du chemin de décodage PNG
 * réel via LVGL/LODEPNG (voir lv_lodepng_init(), appelée une fois pour toute
 * la suite dans main.c -- LV_USE_LODEPNG activé dans simulateur/lv_conf.h,
 * PARTAGÉE avec ce harnais, voir host-test/CMakeLists.txt).
 *
 * Toutes les allocations de tampons "PSRAM" ici sont de simples malloc()
 * (host-test n'a pas de PSRAM -- voir miniature.c, MINIATURE_FREE retombe
 * sur free() hors ESP_PLATFORM) : miniature_poser_prete() en prend
 * possession, ne JAMAIS les libérer soi-même après un appel accepté ou
 * après un appel dont on ne connaît pas déjà l'issue -- voir le commentaire
 * de chaque section pour qui possède quoi.
 *
 * Store process-wide (singleton de fichier, comme klipper_fichiers.c/
 * power_devices.c) : aucune contrainte d'ordre avec les autres suites (voir
 * main.c), mais CETTE suite doit rester interne à elle-même dans son usage
 * du store -- chaque section part d'un miniature_effacer() pour repartir
 * d'un état ABSENTE connu. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "src/draw/lv_image_decoder_private.h" /* definition complete de lv_image_decoder_dsc_t,
                                                  * PAS exposee par le lv_image_decoder.h public --
                                                  * necessaire pour declarer une variable LOCALE de
                                                  * ce type (pas seulement un pointeur), voir plus bas. */
#include "src/libs/lodepng/lodepng.h"

#include "miniature.h"
#include "petit_test.h"

/* Signature PNG (8 octets) suivie d'un en-tête IHDR MINIMAL BIDON (16
 * octets de plus, total 24 -- exactement TAILLE_MIN_SURE de miniature.c) :
 * pas un PNG valide au sens de LodePNG (pas de vraies données IDAT), mais
 * SUFFISANT pour passer le filtre ressemble_a_un_png() du store (signature +
 * taille), ce qui est tout ce que ces sections de STORE veulent exercer --
 * le décodage LVGL réel est testé séparément plus bas avec un VRAI PNG
 * généré par lodepng_encode32(). */
static void construire_faux_png_24_octets(uint8_t *dest)
{
    static const uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(dest, SIGNATURE, sizeof(SIGNATURE));
    memset(dest + sizeof(SIGNATURE), 0, 24 - sizeof(SIGNATURE));
}

/* --- store : cycle de vie nominal (en_cours -> prete -> effacer) --------- */

static void section_store_cycle_nominal(void)
{
    miniature_effacer(); /* repart d'un etat connu, meme si une suite anterieure a touche le store */

    miniature_instantane_t snap;
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_ABSENTE);
    VERIFIER(snap.donnees == NULL);
    uint32_t gen0 = snap.generation;

    miniature_poser_en_cours("piece.gcode");
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_EN_COURS);
    VERIFIER_TEXTE(snap.fichier, "piece.gcode");
    VERIFIER(snap.donnees == NULL);
    VERIFIER(snap.generation != gen0);
    uint32_t gen1 = snap.generation;

    uint8_t *donnees = (uint8_t *)malloc(24);
    construire_faux_png_24_octets(donnees);
    miniature_poser_prete("piece.gcode", donnees, 24, 100, 100);
    /* miniature_poser_prete() possede desormais `donnees` -- ne plus jamais
     * le toucher depuis ce test. */

    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_PRETE);
    VERIFIER(snap.donnees != NULL);
    VERIFIER(snap.taille == 24);
    VERIFIER(snap.largeur == 100);
    VERIFIER(snap.hauteur == 100);
    VERIFIER(snap.generation != gen1);

    /* purger() sans rien en attente : sans effet, ne doit pas planter. */
    miniature_purger(NULL);
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_PRETE); /* toujours la, purger() n'efface pas l'actif */

    /* fin d'impression -> ABSENTE, l'ancien tampon retire (transfert
     * differe) puis libere par la purge qui suit (ASan/LSan verifient
     * l'absence de fuite en fin de suite hote -- voir le commentaire de
     * tete de ce fichier). */
    miniature_effacer();
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_ABSENTE);
    VERIFIER(snap.donnees == NULL);
    VERIFIER_TEXTE(snap.fichier, "");
    miniature_purger(NULL);
}

/* --- store : garde de pertinence (fichier perime) ------------------------ */

static void section_store_garde_pertinence(void)
{
    miniature_effacer();

    miniature_poser_en_cours("a.gcode");
    /* Un resultat pour un AUTRE fichier (deja depasse par un declenchement
     * plus recent, cote reel -- moonraker_ws.c) est rejete : le tampon
     * fourni est libere IMMEDIATEMENT par le store (ASan confirmera
     * l'absence de fuite), l'etat suivi pour "a.gcode" reste intact. */
    uint8_t *donnees_b = (uint8_t *)malloc(24);
    construire_faux_png_24_octets(donnees_b);
    miniature_poser_prete("b.gcode", donnees_b, 24, 10, 10);

    miniature_instantane_t snap;
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_EN_COURS); /* inchange -- le depot pour "b.gcode" a ete ignore */
    VERIFIER_TEXTE(snap.fichier, "a.gcode");

    /* Meme garde pour un echec perime. */
    miniature_poser_echec("b.gcode");
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_EN_COURS); /* toujours inchange */

    /* Le VRAI resultat pour "a.gcode", lui, est accepte. */
    uint8_t *donnees_a = (uint8_t *)malloc(24);
    construire_faux_png_24_octets(donnees_a);
    miniature_poser_prete("a.gcode", donnees_a, 24, 20, 20);
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_PRETE);
    VERIFIER_TEXTE(snap.fichier, "a.gcode");

    /* Un echec pour "a.gcode" lui-meme (fetch retente et rate, scenario
     * hypothetique) EST accepte -- retire le tampon PRET (transfert
     * differe). */
    miniature_poser_echec("a.gcode");
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_ECHEC);
    VERIFIER(snap.donnees == NULL);
    miniature_purger(NULL);

    miniature_effacer();
    miniature_purger(NULL);
}

/* --- store : entrees degenerees (jamais de crash, jamais de fuite) ------- */

static void section_store_entrees_degenerees(void)
{
    miniature_effacer();

    /* Un tampon fourni mais rejete (fichier NULL/vide, taille 0, donnees
     * NULL avec taille non nulle, signature absente, trop court) est
     * TOUJOURS libere par le store si non-NULL -- jamais fuite. */
    miniature_poser_prete(NULL, (uint8_t *)malloc(24), 24, 1, 1);
    miniature_poser_prete("", (uint8_t *)malloc(24), 24, 1, 1);
    miniature_poser_prete("x.gcode", NULL, 0, 1, 1);

    uint8_t court[8];
    memcpy(court, "\x89PNG\r\n\x1a\n", 8); /* signature seule -- < TAILLE_MIN_SURE (24), voir miniature.c */
    uint8_t *court_alloue = (uint8_t *)malloc(sizeof(court));
    memcpy(court_alloue, court, sizeof(court));
    miniature_poser_prete("x.gcode", court_alloue, sizeof(court), 1, 1);

    uint8_t *pas_png = (uint8_t *)malloc(24);
    memset(pas_png, 0x42, 24); /* ne commence pas par la signature PNG */
    miniature_poser_prete("x.gcode", pas_png, 24, 1, 1);

    /* Aucun de ces depots (tous rejetes -- "x.gcode" n'a jamais ete pose en
     * cours) n'a du modifier le store, deja remis a ABSENTE en tete de
     * section. */
    miniature_instantane_t snap;
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_ABSENTE);
    VERIFIER(snap.donnees == NULL);

    /* fichier/nom NULL ou vide : no-op sans crash pour les trois autres
     * fonctions de depot. */
    miniature_poser_en_cours(NULL);
    miniature_lire(&snap);
    VERIFIER_TEXTE(snap.fichier, ""); /* NULL traite comme chaine vide, voir miniature.h */

    miniature_poser_echec(NULL);
    miniature_poser_echec("");
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_EN_COURS); /* aucune des deux gardes n'a rien change */

    miniature_lire(NULL); /* ne doit pas crasher */

    miniature_effacer();
    miniature_purger(NULL);
}

/* --- store : voie residuelle du producteur (slot deja occupe) -- doit rester
 * sans fuite, sans crash, sans double-free. --------------------------------
 *
 * Depuis le fix use-after-free (revue opus) un tampon n'est DIFFERE dans le
 * slot g_a_liberer que s'il est celui que le consommateur DETIENT (revendique
 * par miniature_lire()) ; tout autre tampon retire est libere immediatement et
 * surement (jamais reference par un widget). Pour atteindre la voie residuelle
 * (retirer un tampon DETENU alors que le slot porte deja un AUTRE tampon
 * detenu anterieurement), il faut donc revendiquer (lire) entre chaque depot,
 * en simulant un consommateur qui affiche puis se detourne. */

static void section_store_voie_residuelle_producteur(void)
{
    miniature_effacer();
    miniature_purger(NULL); /* repart d'un g_affiche/slot propres */
    miniature_poser_en_cours("x.gcode");

    miniature_instantane_t snap;

    uint8_t *bufA = (uint8_t *)malloc(24);
    construire_faux_png_24_octets(bufA);
    miniature_poser_prete("x.gcode", bufA, 24, 1, 1);
    miniature_lire(&snap); /* le consommateur PREND bufA (g_affiche = bufA) */
    VERIFIER(snap.donnees == bufA);

    /* Nouveau depot pour le MEME fichier : retire bufA. bufA est DETENU
     * (== g_affiche) -> DIFFERE dans le slot, jamais libere ici. */
    uint8_t *bufB = (uint8_t *)malloc(24);
    construire_faux_png_24_octets(bufB);
    miniature_poser_prete("x.gcode", bufB, 24, 1, 1);
    miniature_lire(&snap); /* le consommateur se detourne de bufA et PREND bufB */
    VERIFIER(snap.donnees == bufB);

    /* 3e depot : retire bufB (DETENU) alors que le slot porte encore bufA.
     * Voie residuelle : bufA n'est PLUS detenu (g_affiche vaut bufB) -> libere
     * ICI, sur la tache productrice ; bufB prend sa place dans le slot. ASan
     * confirmera l'absence de fuite comme de double-free. */
    uint8_t *bufC = (uint8_t *)malloc(24);
    construire_faux_png_24_octets(bufC);
    miniature_poser_prete("x.gcode", bufC, 24, 1, 1);

    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_PRETE);
    VERIFIER(snap.donnees == bufC);

    /* Le consommateur affiche bufC : la purge libere le slot (bufB, plus
     * detenu) mais protege bufC (encore_affiche == bufC). */
    miniature_purger(bufC);

    /* Le consommateur se detourne (plus rien affiche) puis fin d'impression :
     * bufC, retire et plus detenu, est libere par la derniere purge. */
    miniature_effacer();
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_ABSENTE);
    miniature_purger(NULL);
}

/* --- store : REPRODUCTION du use-after-free de la revue (scenario a) -------
 *
 * Un depot producteur survient ENTRE le miniature_lire() du consommateur et sa
 * miniature_purger() -- exactement la fenetre du rapport de revue. Le tampon
 * que le consommateur vient de poser sur le widget (et que le dessin DIFFERE
 * de LVGL decodera plus tard) ne doit PAS etre libere par cette purge. On le
 * prouve en DECODANT reellement les octets (lv_image_decoder_get_info lit le
 * tampon) APRES la purge : si le fix etait absent (purge liberant le slot sans
 * regarder encore_affiche), ASan signalerait un heap-use-after-free ici. */

static void section_store_uaf_depot_entre_lire_et_purger(void)
{
    miniature_effacer();
    miniature_purger(NULL); /* g_affiche/slot propres */

    /* Vrai PNG (encode par lodepng) copie dans un tampon "PSRAM" (malloc,
     * possede par le store -- MINIATURE_FREE retombe sur free() hors ESP). */
    uint8_t pixels[2 * 2 * 4] = {
        255, 0,   0,   255,
        0,   255, 0,   255,
        0,   0,   255, 255,
        0,   0,   0,   0,
    };
    uint8_t *png = NULL;
    size_t   png_taille = 0;
    unsigned erreur = lodepng_encode32(&png, &png_taille, pixels, 2, 2);
    VERIFIER(erreur == 0);
    VERIFIER(png != NULL);
    if (erreur != 0 || png == NULL) {
        return;
    }
    VERIFIER(png_taille >= 24);
    uint8_t *P = (uint8_t *)malloc(png_taille);
    memcpy(P, png, png_taille);
    lv_free(png); /* lodepng vendorise = lv_malloc, voir section_decodage_lvgl_reel */

    miniature_poser_en_cours("f.gcode");
    miniature_poser_prete("f.gcode", P, png_taille, 2, 2);

    /* Le consommateur LIT l'instantane : fige snap.donnees == P et le
     * REVENDIQUE (g_affiche = P, sous le meme verrou). */
    miniature_instantane_t snap;
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_PRETE);
    VERIFIER(snap.donnees == P);

    /* Depot producteur GLISSE entre le lire et le purger (fin d'impression sur
     * la tache WS, parallele reel sur l'ESP) : retire P. P etant DETENU, il est
     * seulement differe -- jamais libere. */
    miniature_effacer();

    /* Le consommateur pose P sur le widget puis DESSINE : on decode reellement
     * P ici (equivaut au dessin differe de LVGL). Doit reussir -- P vivant. */
    lv_image_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RAW;
    dsc.data = P;
    dsc.data_size = (uint32_t)png_taille;
    lv_image_header_t header;
    memset(&header, 0, sizeof(header));
    VERIFIER(lv_image_decoder_get_info(&dsc, &header) == LV_RESULT_OK);
    VERIFIER(header.w == 2 && header.h == 2);

    /* Purge en confirmant que P est ENCORE sur le widget (encore_affiche == P) :
     * le store DOIT refuser de le liberer. */
    miniature_purger(P);

    /* Preuve directe de l'absence d'UAF : re-decodage de P APRES la purge.
     * Sous l'ancien code (purge liberant le slot inconditionnellement), cette
     * lecture porterait sur de la memoire liberee -> ASan aborterait. */
    memset(&header, 0, sizeof(header));
    VERIFIER(lv_image_decoder_get_info(&dsc, &header) == LV_RESULT_OK);
    VERIFIER(header.w == 2 && header.h == 2);

    /* Cycle suivant : etat ABSENTE, le consommateur masque le widget et
     * n'affiche plus P (encore_affiche = NULL) -> P devient liberable et est
     * libere par cette purge. Aucune fuite (ASan/LSan en fin de suite). */
    miniature_lire(&snap);
    VERIFIER(snap.etat == MINIATURE_ABSENTE);
    miniature_purger(NULL);

    miniature_effacer();
    miniature_purger(NULL);
}

/* --- de-risking recommande par le brief : PROUVER le chemin de decodage
 * LVGL/LODEPNG reel (pas seulement le filtre du store) --------------------
 *
 * Genere un VRAI petit PNG a l'aide de l'ENCODEUR lodepng (vendorise dans ce
 * meme depot, LODEPNG_COMPILE_ENCODER actif par defaut -- voir lodepng.h) :
 * plus fiable qu'un tableau d'octets recopie a la main (un mauvais CRC32
 * ferait echouer le decodage et fausserait ce test), et prouve un VRAI
 * aller-retour encode -> decode via l'API LVGL publique (lv_image_decoder_*),
 * exactement le chemin qu'emprunte ecran_accueil.c en pratique
 * (lv_image_decoder_get_info() avant tout lv_image_set_src(), voir son
 * commentaire dans ecran_accueil.c). */
static void section_decodage_lvgl_reel(void)
{
    /* 2x2 RGBA, quatre couleurs distinctes -- peu importe le contenu visuel,
     * seul le ROUND-TRIP structurel (dimensions, absence de fuite/crash)
     * compte ici. */
    uint8_t pixels[2 * 2 * 4] = {
        255, 0,   0,   255, /* rouge opaque */
        0,   255, 0,   255, /* vert opaque */
        0,   0,   255, 255, /* bleu opaque */
        0,   0,   0,   0,   /* transparent */
    };

    uint8_t *png = NULL;
    size_t   png_taille = 0;
    unsigned erreur = lodepng_encode32(&png, &png_taille, pixels, 2, 2);
    VERIFIER(erreur == 0);
    VERIFIER(png != NULL);
    if (erreur != 0 || png == NULL) {
        return; /* rien de plus a prouver si l'encodage lui-meme a echoue */
    }
    VERIFIER(png_taille >= 24);

    lv_image_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC; /* classe la source LV_IMAGE_SRC_VARIABLE, voir lv_draw_image.c */
    dsc.header.cf = LV_COLOR_FORMAT_RAW;      /* ignore par LODEPNG, voir ecran_accueil.c */
    dsc.data = png;
    dsc.data_size = (uint32_t)png_taille;

    /* 1) Info seule (lit la signature + l'entete IHDR, pas de decodage
     * pixel complet) -- meme appel que mettre_a_jour_miniature() dans
     * ecran_accueil.c. */
    lv_image_header_t header;
    memset(&header, 0, sizeof(header));
    lv_result_t r_info = lv_image_decoder_get_info(&dsc, &header);
    VERIFIER(r_info == LV_RESULT_OK);
    VERIFIER(header.w == 2);
    VERIFIER(header.h == 2);

    /* 2) Decodage pixel complet (decoder_open/close) -- prouve que LODEPNG
     * est bien enregistre (lv_lodepng_init(), appele une fois dans
     * initialiser_affichage_test(), main.c) et que le cycle complet ne fuit
     * ni ne plante (ASan/LSan verifient a la sortie du processus). */
    lv_image_decoder_dsc_t ddsc;
    memset(&ddsc, 0, sizeof(ddsc));
    lv_result_t r_open = lv_image_decoder_open(&ddsc, &dsc, NULL);
    VERIFIER(r_open == LV_RESULT_OK);
    if (r_open == LV_RESULT_OK) {
        VERIFIER(ddsc.decoded != NULL);
        lv_image_decoder_close(&ddsc);
    }

    /* lv_free(), PAS free() -- piège découvert en écrivant ce test : le
     * commentaire PUBLIC de lodepng_encode32() (lodepng.h, "Must be freed
     * ... with free()") documente le comportement UPSTREAM de LodePNG, mais
     * CETTE COPIE vendorisée dans LVGL (lodepng.c, voir sa tête de fichier)
     * redirige silencieusement lodepng_malloc/free/realloc vers
     * lv_malloc/lv_free/lv_realloc (LODEPNG_COMPILE_ALLOCATORS) -- SANS
     * mettre à jour ce commentaire public. Appeler le vrai `free()` libc ici
     * est un bad-free (ASan le détecte immédiatement, "wild pointer") : le
     * bloc a été alloué par l'allocateur TLSF interne de LVGL, pas par
     * malloc(). `lodepng_free()` elle-même est `static` dans lodepng.c (non
     * exportée) -- `lv_free()` (API LVGL publique) est le seul contrepoint
     * correct et appelable depuis l'extérieur. */
    lv_free(png);

    /* --- robustesse : un flux TRONQUE (signature valide, reste absent ou
     * mutile) ne doit JAMAIS planter le decodeur, seulement echouer
     * proprement -- coeur de l'exigence "jamais de crash si le PNG est
     * invalide/tronque" du brief. Tronque a MI-CHEMIN du fichier reel
     * (largement >= 24 octets, donc jamais le bogue de lecture hors bornes
     * documente dans miniature.c pour un tampon < 24 -- ce test-ci vise la
     * robustesse du DECODAGE PIXEL sur un flux structurellement incomplet
     * mais dont l'EN-TETE, lui, est intact et lisible). */
    unsigned char *png2 = NULL;
    size_t         png2_taille = 0;
    erreur = lodepng_encode32(&png2, &png2_taille, pixels, 2, 2);
    VERIFIER(erreur == 0);
    if (erreur == 0 && png2 != NULL && png2_taille > 30) {
        size_t taille_tronquee = png2_taille / 2;
        VERIFIER(taille_tronquee >= 24);

        lv_image_dsc_t dsc_tronque;
        memset(&dsc_tronque, 0, sizeof(dsc_tronque));
        dsc_tronque.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc_tronque.header.cf = LV_COLOR_FORMAT_RAW;
        dsc_tronque.data = png2;
        dsc_tronque.data_size = (uint32_t)taille_tronquee;

        /* L'en-tete (24 premiers octets) reste lisible -- get_info() reussit
         * toujours et rend les vraies dimensions (elles sont ecrites AVANT
         * le corps compresse tronque). */
        lv_image_header_t header_tronque;
        memset(&header_tronque, 0, sizeof(header_tronque));
        lv_result_t r_info_tronque = lv_image_decoder_get_info(&dsc_tronque, &header_tronque);
        VERIFIER(r_info_tronque == LV_RESULT_OK);

        /* Le decodage PIXEL complet, lui, doit echouer PROPREMENT (flux
         * DEFLATE incomplet -- LodePNG le detecte et rend une erreur) plutot
         * que de planter ou de lire au-dela du tampon fourni. */
        lv_image_decoder_dsc_t ddsc_tronque;
        memset(&ddsc_tronque, 0, sizeof(ddsc_tronque));
        lv_result_t r_open_tronque = lv_image_decoder_open(&ddsc_tronque, &dsc_tronque, NULL);
        VERIFIER(r_open_tronque == LV_RESULT_INVALID);
        if (r_open_tronque == LV_RESULT_OK) {
            /* Ne devrait jamais arriver (voir la verification ci-dessus) --
             * si jamais LodePNG acceptait quand meme un flux tronque, ne pas
             * fuir le decode obtenu. */
            lv_image_decoder_close(&ddsc_tronque);
        }
    }
    if (png2 != NULL) {
        lv_free(png2); /* voir le commentaire sur lv_free(png) plus haut -- meme piege */
    }
}

void suite_miniature(void)
{
    printf("suite : miniature (feature Miniatures gcode, tache B)\n");

    section_store_cycle_nominal();
    section_store_garde_pertinence();
    section_store_entrees_degenerees();
    section_store_voie_residuelle_producteur();
    section_store_uaf_depot_entre_lire_et_purger();
    section_decodage_lvgl_reel();
}
