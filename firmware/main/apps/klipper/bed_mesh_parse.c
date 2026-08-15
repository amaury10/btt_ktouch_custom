/* Implémentation de bed_mesh_parse.h -- voir ce header. Mêmes idiomes cJSON
 * que moonraker_rpc.c (GetObjectItemCaseSensitive, IsNumber avant lecture,
 * fusion par champ, jamais de destruction de ce que le JSON ne porte pas). */
#include "bed_mesh_parse.h"

#include <stdio.h> /* snprintf -- copie bornee du nom de profil (revue L9) */
#include <string.h>

#include "cJSON.h"

/* mesh_min / mesh_max : tableaux [x, y]. Fusion silencieusement ignorée si
 * la forme n'est pas celle attendue -- un champ malformé ne poisonne pas le
 * reste (politique nombre_fini() de moonraker_rpc.c). */
static void fusionner_paire(const cJSON *tableau, float *x, float *y)
{
    if (!cJSON_IsArray(tableau) || cJSON_GetArraySize(tableau) < 2) {
        return;
    }
    const cJSON *premier = cJSON_GetArrayItem(tableau, 0);
    const cJSON *second = cJSON_GetArrayItem(tableau, 1);
    if (cJSON_IsNumber(premier)) {
        *x = (float)premier->valuedouble;
    }
    if (cJSON_IsNumber(second)) {
        *y = (float)second->valuedouble;
    }
}

bool bed_mesh_fusionner_json(bed_mesh_t *mesh, const struct cJSON *objet)
{
    const cJSON *o = (const cJSON *)objet;
    if (mesh == NULL || !cJSON_IsObject(o)) {
        return false;
    }

    const cJSON *profil = cJSON_GetObjectItemCaseSensitive(o, "profile_name");
    if (cJSON_IsString(profil) && profil->valuestring != NULL) {
        /* snprintf "%s" : source de longueur inconnue (cJSON), pas de
           -Wformat-truncation possible -- idiome fusionner_print_stats
           (revue du 2026-08-15, L9). */
        snprintf(mesh->profil, sizeof(mesh->profil), "%s", profil->valuestring);
    }

    fusionner_paire(cJSON_GetObjectItemCaseSensitive(o, "mesh_min"),
                    &mesh->mesh_min_x, &mesh->mesh_min_y);
    fusionner_paire(cJSON_GetObjectItemCaseSensitive(o, "mesh_max"),
                    &mesh->mesh_max_x, &mesh->mesh_max_y);

    const cJSON *matrice = cJSON_GetObjectItemCaseSensitive(o, "probed_matrix");
    if (cJSON_IsArray(matrice)) {
        int nb_lignes_source = cJSON_GetArraySize(matrice);
        if (nb_lignes_source == 0) {
            /* BED_MESH_CLEAR publie une matrice vide : le mesh disparaît. */
            mesh->present = false;
            mesh->nb_x = 0;
            mesh->nb_y = 0;
            mesh->tronquee = false;
            mesh->z_min = 0.0f;
            mesh->z_max = 0.0f;
        } else {
            /* DEUX passes sur le cJSON (revue du 2026-08-15, L2/L7, sans
               tampon de pile) : la passe 1 VALIDE -- rectangulaire (lignes
               de même largeur), que des nombres -- sans rien écrire ; la
               passe 2 écrit dans mesh->z. Une matrice malformée est ainsi
               IGNORÉE en bloc : l'ancienne version effaçait un mesh valide
               (L2) ou laissait des cellules périmées affichées comme
               mesurées (L7). Klipper réel envoie toujours rectangulaire :
               tout écart est une corruption, pas une donnée. */
            bool tronquee = false;
            int  nb_colonnes_attendu = -1;
            bool saine = true;

            if (nb_lignes_source > BED_MESH_MAX) {
                nb_lignes_source = BED_MESH_MAX;
                tronquee = true;
            }
            for (int ligne = 0; ligne < nb_lignes_source && saine; ligne++) {
                const cJSON *rangee = cJSON_GetArrayItem(matrice, ligne);
                if (!cJSON_IsArray(rangee)) {
                    saine = false;
                    break;
                }
                int nb_colonnes = cJSON_GetArraySize(rangee);
                if (nb_colonnes > BED_MESH_MAX) {
                    nb_colonnes = BED_MESH_MAX;
                    tronquee = true;
                }
                if (nb_colonnes_attendu < 0) {
                    nb_colonnes_attendu = nb_colonnes;
                } else if (nb_colonnes != nb_colonnes_attendu) {
                    saine = false;
                    break;
                }
                for (int colonne = 0; colonne < nb_colonnes; colonne++) {
                    if (!cJSON_IsNumber(cJSON_GetArrayItem(rangee, colonne))) {
                        saine = false;
                        break;
                    }
                }
            }

            if (saine && nb_colonnes_attendu > 0 && nb_lignes_source > 0) {
                float z_min = 0.0f;
                float z_max = 0.0f;
                bool  premier_point = true;
                for (int ligne = 0; ligne < nb_lignes_source; ligne++) {
                    const cJSON *rangee = cJSON_GetArrayItem(matrice, ligne);
                    for (int colonne = 0; colonne < nb_colonnes_attendu; colonne++) {
                        float z = (float)cJSON_GetArrayItem(rangee, colonne)->valuedouble;
                        mesh->z[ligne][colonne] = z;
                        if (premier_point || z < z_min) {
                            z_min = z;
                        }
                        if (premier_point || z > z_max) {
                            z_max = z;
                        }
                        premier_point = false;
                    }
                }
                mesh->nb_x = (uint8_t)nb_colonnes_attendu;
                mesh->nb_y = (uint8_t)nb_lignes_source;
                mesh->tronquee = tronquee;
                mesh->z_min = z_min;
                mesh->z_max = z_max;
                mesh->present = true;
            }
            /* !saine : matrice ignorée, le mesh précédent reste intact. */
        }
    }

    return true;
}
