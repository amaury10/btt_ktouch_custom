/* Bed Mesh (spec 2026-08-15-bed-mesh-input-shaper-design.md) : store PSRAM
 * dédié de la matrice mesurée + parseur pur du sous-objet JSON "bed_mesh"
 * de Moonraker. Store process-wide : chaque section repart d'un definir()
 * propre -- aucune contrainte d'ordre avec les autres suites. */
#include <string.h>

#include "bed_mesh_parse.h"
#include "bed_mesh_store.h"
#include "cJSON.h"
#include "petit_test.h"

/* Sous-objet bed_mesh réaliste (raccourci d'une réponse Moonraker réelle,
 * matrice 3x3, profil "default"). */
static const char JSON_NOMINAL[] =
    "{\"profile_name\":\"default\","
    "\"mesh_min\":[10.0,15.0],\"mesh_max\":[210.0,215.0],"
    "\"probed_matrix\":[[0.01,0.02,0.03],[-0.01,0.0,0.01],[-0.05,0.02,0.10]]}";

static cJSON *parser(const char *texte)
{
    cJSON *racine = cJSON_Parse(texte);
    VERIFIER(racine != NULL);
    return racine;
}

static void section_parse_nominal(void)
{
    bed_mesh_t mesh;
    memset(&mesh, 0, sizeof(mesh));

    cJSON *objet = parser(JSON_NOMINAL);
    VERIFIER(bed_mesh_fusionner_json(&mesh, objet));
    cJSON_Delete(objet);

    VERIFIER(mesh.present);
    VERIFIER_TEXTE(mesh.profil, "default");
    VERIFIER(mesh.nb_x == 3 && mesh.nb_y == 3);
    VERIFIER(!mesh.tronquee);
    VERIFIER(mesh.mesh_min_x > 9.9f && mesh.mesh_min_x < 10.1f);
    VERIFIER(mesh.mesh_max_y > 214.9f && mesh.mesh_max_y < 215.1f);
    /* [ligne Y][colonne X] : la 3e ligne, 3e colonne vaut 0.10. */
    VERIFIER(mesh.z[2][2] > 0.099f && mesh.z[2][2] < 0.101f);
    /* Bornes recalculées sur la matrice. */
    VERIFIER(mesh.z_min > -0.051f && mesh.z_min < -0.049f);
    VERIFIER(mesh.z_max > 0.099f && mesh.z_max < 0.101f);
}

static void section_parse_partiel_et_invalide(void)
{
    bed_mesh_t mesh;
    memset(&mesh, 0, sizeof(mesh));

    /* Fusion par champ : un notify partiel (profile_name seul) ne détruit
     * pas une matrice déjà en place. */
    cJSON *objet = parser(JSON_NOMINAL);
    VERIFIER(bed_mesh_fusionner_json(&mesh, objet));
    cJSON_Delete(objet);
    objet = parser("{\"profile_name\":\"autre\"}");
    VERIFIER(bed_mesh_fusionner_json(&mesh, objet));
    cJSON_Delete(objet);
    VERIFIER_TEXTE(mesh.profil, "autre");
    VERIFIER(mesh.nb_x == 3); /* matrice intacte */

    /* Matrice vide (BED_MESH_CLEAR publie profile_name "" + matrice []) :
     * present retombe. */
    objet = parser("{\"profile_name\":\"\",\"probed_matrix\":[]}");
    VERIFIER(bed_mesh_fusionner_json(&mesh, objet));
    cJSON_Delete(objet);
    VERIFIER(!mesh.present);
    VERIFIER(mesh.nb_x == 0);

    /* NULL / objet non-objet : false, mesh inchangé. */
    VERIFIER(!bed_mesh_fusionner_json(&mesh, NULL));
    VERIFIER(!bed_mesh_fusionner_json(NULL, NULL));
}

static void section_parse_troncature(void)
{
    /* Matrice 30 colonnes x 2 lignes : colonnes tronquées à BED_MESH_MAX
     * (25 depuis le retour matériel 21x21 du 2026-08-15 -- 20 colonnes ne
     * tronquent PLUS), drapeau levé. */
    char json[1024];
    size_t pos = 0;
    pos += (size_t)snprintf(json + pos, sizeof(json) - pos, "{\"probed_matrix\":[");
    for (int ligne = 0; ligne < 2; ligne++) {
        pos += (size_t)snprintf(json + pos, sizeof(json) - pos, "%s[", ligne ? "," : "");
        for (int colonne = 0; colonne < 30; colonne++) {
            pos += (size_t)snprintf(json + pos, sizeof(json) - pos, "%s0.%02d",
                                    colonne ? "," : "", colonne);
        }
        pos += (size_t)snprintf(json + pos, sizeof(json) - pos, "]");
    }
    (void)snprintf(json + pos, sizeof(json) - pos, "]}");

    bed_mesh_t mesh;
    memset(&mesh, 0, sizeof(mesh));
    cJSON *objet = parser(json);
    VERIFIER(bed_mesh_fusionner_json(&mesh, objet));
    cJSON_Delete(objet);
    VERIFIER(mesh.nb_x == BED_MESH_MAX);
    VERIFIER(mesh.nb_y == 2);
    VERIFIER(mesh.tronquee);
}

static void section_store(void)
{
    bed_mesh_t mesh;
    memset(&mesh, 0, sizeof(mesh));
    mesh.present = true;
    strcpy(mesh.profil, "default");
    mesh.nb_x = 2;
    mesh.nb_y = 2;
    mesh.z[0][0] = -0.1f;
    mesh.z[1][1] = 0.2f;
    mesh.z_min = -0.1f;
    mesh.z_max = 0.2f;

    uint32_t generation_avant = bed_mesh_generation();
    bed_mesh_definir(&mesh);
    VERIFIER(bed_mesh_generation() == generation_avant + 1);

    bed_mesh_t lu;
    bed_mesh_lire(&lu);
    VERIFIER(lu.present);
    VERIFIER_TEXTE(lu.profil, "default");
    VERIFIER(lu.z[1][1] > 0.19f && lu.z[1][1] < 0.21f);

    /* Gardes NULL : jamais de crash, pas de génération fantôme. */
    generation_avant = bed_mesh_generation();
    bed_mesh_definir(NULL);
    VERIFIER(bed_mesh_generation() == generation_avant);
    bed_mesh_lire(NULL);
}

static void section_position_point(void)
{
    bed_mesh_t mesh;
    memset(&mesh, 0, sizeof(mesh));
    mesh.present = true;
    mesh.nb_x = 21;
    mesh.nb_y = 21;
    mesh.mesh_min_x = 10.0f;
    mesh.mesh_min_y = 20.0f;
    mesh.mesh_max_x = 410.0f;
    mesh.mesh_max_y = 420.0f;

    float x = -1.0f, y = -1.0f;
    /* Coins : bornes exactes. */
    VERIFIER(bed_mesh_position_point(&mesh, 0, 0, &x, &y));
    VERIFIER(x > 9.9f && x < 10.1f);
    VERIFIER(y > 19.9f && y < 20.1f);
    VERIFIER(bed_mesh_position_point(&mesh, 20, 20, &x, &y));
    VERIFIER(x > 409.9f && x < 410.1f);
    VERIFIER(y > 419.9f && y < 420.1f);
    /* Milieu : interpolation lineaire (colonne 10 sur 0..20 = 210.0). */
    VERIFIER(bed_mesh_position_point(&mesh, 10, 10, &x, &y));
    VERIFIER(x > 209.9f && x < 210.1f);
    VERIFIER(y > 219.9f && y < 220.1f);
    /* Axe a un point : la borne min. */
    mesh.nb_x = 1;
    VERIFIER(bed_mesh_position_point(&mesh, 5, 0, &x, &y));
    VERIFIER(x > 9.9f && x < 10.1f);
    /* Refus : indice hors bornes, mesh absent, NULL -- x/y intacts. */
    x = -99.0f;
    VERIFIER(!bed_mesh_position_point(&mesh, 5, 1, &x, &y));
    VERIFIER(!bed_mesh_position_point(&mesh, 21, 0, &x, &y));
    mesh.present = false;
    VERIFIER(!bed_mesh_position_point(&mesh, 0, 0, &x, &y));
    VERIFIER(!bed_mesh_position_point(NULL, 0, 0, &x, &y));
    VERIFIER(x < -98.0f);
}

static void section_profils_store(void)
{
    bed_mesh_profils_t profils;
    memset(&profils, 0, sizeof(profils));
    profils.nb = 2;
    strcpy(profils.noms[0], "default");
    strcpy(profils.noms[1], "textured_pei");

    uint32_t generation_avant = bed_mesh_generation();
    bed_mesh_profils_definir(&profils);
    VERIFIER(bed_mesh_generation() == generation_avant + 1); /* MEME compteur que la carte */

    bed_mesh_profils_t lus;
    memset(&lus, 0xFF, sizeof(lus));
    bed_mesh_profils_lire(&lus);
    VERIFIER(lus.nb == 2);
    VERIFIER(!lus.tronques);
    VERIFIER_TEXTE(lus.noms[0], "default");
    VERIFIER_TEXTE(lus.noms[1], "textured_pei");

    /* Garde defensive : nb au-dela du plafond est ramene au plafond. */
    profils.nb = BED_MESH_PROFILS_MAX + 3;
    bed_mesh_profils_definir(&profils);
    bed_mesh_profils_lire(&lus);
    VERIFIER(lus.nb == BED_MESH_PROFILS_MAX);

    /* NULL : jamais de crash, pas de generation fantome. */
    generation_avant = bed_mesh_generation();
    bed_mesh_profils_definir(NULL);
    VERIFIER(bed_mesh_generation() == generation_avant);
    bed_mesh_profils_lire(NULL);
}

void suite_bed_mesh(void)
{
    printf("suite : bed mesh (store + parseur du sous-objet Moonraker)\n");
    section_parse_nominal();
    section_parse_partiel_et_invalide();
    section_parse_troncature();
    section_store();
    section_position_point();
    section_profils_store();
}
