# Miniatures gcode — Design (v1)

**Date :** 2026-08-04
**Contexte :** aujourd'hui le décodage PNG est DÉSACTIVÉ (`sdkconfig` :
`# CONFIG_LV_USE_LODEPNG is not set`), aucune miniature n'est récupérée de
Moonraker, et `etat_klipper_t` ne contient aucun champ miniature. Objectif v1
volontairement étroit pour maîtriser la RAM/PSRAM : **afficher la miniature du
fichier EN COURS d'impression sur l'écran d'impression** (`ecran_accueil.c`), un
seul thumbnail, fetché une fois par impression.

## Périmètre v1 (étroit et sûr)

- UNE miniature à la fois : celle du fichier actif (`print_stats.filename`).
- Fetch déclenché quand une impression est active et que le nom de fichier est
  connu/nouveau ; libéré quand l'impression se termine ou change.
- PAS de miniatures dans la liste de fichiers (N fetches/décodages) — reporté v2.

## Chaîne Moonraker

1. `server.files.metadata` params `{"filename":"<chemin gcode>"}` → `result`
   contient `thumbnails:[{width,height,size,relative_path},...]`. `relative_path`
   est relatif au DOSSIER du fichier gcode (ex. `.thumbs/foo-300x300.png`).
2. Choix : la plus grande miniature dont `width<=TAILLE_MAX_PX` (borne mémoire),
   sinon la plus petite. Construire l'URL HTTP :
   `http://<hote>:<port>/server/files/gcodes/<dossier_du_gcode>/<relative_path>`
   (le `relative_path` est relatif au dossier du gcode — attention à recomposer
   correctement le chemin quand le gcode est dans un sous-dossier).
3. HTTP GET → octets PNG dans un tampon **PSRAM** → décodage LVGL LODEPNG →
   `lv_image` sur l'écran d'impression.

## Architecture

### Task A — cœur host-testable

- Parseur pur `bool rpc_lire_miniature(char *chemin_dest, size_t chemin_n,
  int *w_out, int *h_out, const char *json, size_t n, int largeur_max)` dans
  `moonraker_rpc.{h,c}` (patron `rpc_lire_fichiers`) : parse la réponse
  `server.files.metadata`, choisit la miniature ≤ `largeur_max` la plus grande
  (sinon la plus petite disponible), remplit `relative_path` + dimensions.
  false si aucune miniature. Robuste aux formes inattendues.
- Fonction pure `size_t miniature_construire_chemin(char *dest, size_t n,
  const char *gcode_chemin, const char *relative_path)` : recompose le chemin
  relatif à la racine gcodes (`<dossier de gcode_chemin>/<relative_path>`, gère
  le cas racine sans dossier). Testable hôte (cas racine, sous-dossier, bornage).
- Tests hôte : `rpc_lire_miniature` (0/1/N miniatures, choix par largeur_max,
  formes invalides), `miniature_construire_chemin` (racine, sous-dossier, `../`
  défensif, troncature).

### Task B — intégration ESP

- **sdkconfig** : activer `CONFIG_LV_USE_LODEPNG=y` (dans `firmware/sdkconfig`
  ET `firmware/main/sdkconfig.defaults` pour la persistance). Décodage depuis un
  **buffer mémoire** (source `lv_image_dsc_t` data) — pas besoin d'activer
  `LV_USE_FS_*` (on ne charge pas depuis un chemin de fichier).
- **Fetch HTTP** : helper `esp_http_client` (patron des appels HTTP existants
  dans `backend_moonraker.c`) pour GET l'URL miniature → tampon alloué en PSRAM
  (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`), taille bornée
  (`MINIATURE_TAILLE_MAX_OCTETS`, ex. 64 Ko — refuser au-delà). Sur sa propre
  tâche/contexte pour ne pas bloquer l'UI ; résultat déposé dans un store dédié.
- **Store dédié** `main/apps/klipper/miniature.{h,c}` (patron verrou
  `klipper_fichiers.c`, **hors `etat_klipper_t`**) : détient le PNG décodé
  (ou les octets + un `lv_image_dsc_t`), le nom de fichier associé, un état
  (ABSENTE / EN_COURS / PRETE / ECHEC), une `generation`. L'écran lit l'état et
  affiche/masque l'image. Un seul thumbnail vivant à la fois ; libération
  explicite (PSRAM) au changement de fichier / fin d'impression / destruction.
- **Déclenchement** : quand l'état passe en impression avec un `filename` connu
  et différent du dernier fetché → demander `server.files.metadata` (via le WS,
  nouvel id, patron `g_id_fichiers`), puis lancer le fetch HTTP + décodage.
- **Écran** `ecran_accueil.c` : ajouter un `lv_image` modeste (≤ ~140 px) placé
  sans recouvrir la bande de notification (y absolu 420..480) ni la barre de
  progression ; `mettre_a_jour` affiche l'image quand l'état = PRETE et la
  `generation` a changé, la masque sinon. `_Static_assert` de non-recouvrement.

## Contraintes mémoire (CRITIQUE)

- Le PNG décodé peut être volumineux : **PSRAM uniquement** (`MALLOC_CAP_SPIRAM`),
  jamais la RAM interne, jamais `etat_klipper_t`, jamais sur une pile de tâche.
- Un seul thumbnail décodé à la fois ; libérer l'ancien AVANT d'en décoder un
  nouveau. Bornes strictes (`TAILLE_MAX_PX`, `MINIATURE_TAILLE_MAX_OCTETS`).
- Le tampon de réception HTTP et le buffer décodé LVGL doivent être libérés sur
  tous les chemins (échec HTTP, décodage KO, fin d'impression, `detruire`).
- Vérifier au gate idf la taille binaire et l'absence de régression RAM interne.

## Tests

- Host-test : parseurs purs (Task A).
- idf gate + **matériel** : activation LODEPNG (taille binaire), fetch d'une vraie
  miniature Moonraker, affichage, libération à la fin d'impression, pas de fuite
  PSRAM sur plusieurs impressions successives.

## Hors périmètre (v2+)

- Miniatures dans la liste de fichiers (`ecran_fichiers.c`).
- Cache multi-miniatures. Miniatures embarquées base64 dans le gcode.
