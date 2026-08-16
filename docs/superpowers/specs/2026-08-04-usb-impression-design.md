# Impression depuis USB — Design

**Date :** 2026-08-04
**Décision utilisateur :** l'écran WiFi ne peut pas alimenter Klipper directement
(Klipper imprime depuis l'hôte Moonraker). Approche retenue : **monter la clé,
lister les .gcode, et à la sélection UPLOADER le fichier vers Moonraker puis
lancer l'impression.** L'écran est un pont clé→Moonraker.

**Existant (cf. carte) :** le BSP fournit un hôte USB MSC complet
(`pandatouch_msc.h` : `pt_usb_start`, `pt_usb_on_mount/on_unmount`,
`pt_usb_list_dir("/usb",...)`, `pt_usb_read`, montage FAT `/usb`), **mais `main/`
ne l'appelle nulle part**. Patron de scan : `examples/display_slideshow.c`
(`scan_usb_for_pngs`). Aucun chemin USB→impression n'existe.

## Chaîne

1. Monter la clé (`pt_usb_start` + callbacks au boot). Lister `/usb` récursivement,
   filtrer `.gcode`/`.gco`/`.g` (patron `scan_usb_for_pngs`).
2. Sélection d'un fichier → confirmation → **upload multipart** vers Moonraker
   `POST http://<hote>:<port>/server/files/upload`, champs form-data :
   `root=gcodes`, `print=true`, `file=<contenu>` (filename = nom du .gcode).
   Moonraker stocke le fichier ET lance l'impression (grâce à `print=true`) —
   une seule requête, pas de `SDCARD_PRINT_FILE` séparé.
3. Le fichier peut faire plusieurs Mo → **streaming** : Content-Length calculé à
   l'avance, corps envoyé en morceaux (préambule multipart, puis boucle
   `pt_usb_read`→`esp_http_client_write`, puis trailer). Jamais le fichier entier
   en RAM.

## Architecture

### Task A — cœur host-testable (cadrage multipart + filtre)

Fonctions PURES dans `main/apps/klipper/usb_upload.{h,c}` :
- `bool usb_est_gcode(const char *nom)` — extension `.gcode`/`.gco`/`.g`
  (insensible à la casse). Testable.
- `size_t usb_upload_preambule(char *dest, size_t n, const char *boundary,
  const char *filename)` — construit tout le multipart AVANT les octets fichier
  (parts `root`, `print`, en-tête de la part `file` avec `filename`). Contrat
  retour façon snprintf. Le `filename` est inséré dans un en-tête HTTP : **retirer
  tout `"`/CR/LF du filename** (défense injection d'en-tête) ; documente.
- `size_t usb_upload_trailer(char *dest, size_t n, const char *boundary)` —
  `\r\n--<boundary>--\r\n`.
- `size_t usb_upload_content_length(size_t preambule_len, size_t taille_fichier,
  size_t trailer_len)` — somme (pour l'en-tête Content-Length).
- Tests hôte : `usb_est_gcode` (positifs/négatifs/casse), préambule/trailer
  (contenu exact, bornage), filename avec caractères dangereux nettoyés,
  content-length cohérente.

### Task B — intégration ESP

- **Boot** (`app_main.c`) : `pt_usb_start()` + enregistrer `pt_usb_on_mount`/
  `pt_usb_on_unmount` (mettent à jour un store d'état USB monté/absent).
- **Store dédié** `main/apps/klipper/usb_fichiers.{h,c}` (patron verrou
  `klipper_fichiers.c`, HORS `etat_klipper_t`) : état monté/absent + liste des
  `.gcode` trouvés (bornée `USB_FICHIERS_MAX`, chemins sous `/usb`), `generation`.
  Rempli par un scan (déclenché au mount, patron `scan_usb_for_pngs`).
- **Upload HTTP dédié** `main/apps/klipper/usb_upload_http.{h,c}` (ESP-only, tâche
  dédiée + sémaphore, patron `ota.c`) : ouvre l'`esp_http_client` en POST avec
  Content-Length calculé, écrit le préambule, boucle `pt_usb_read`→
  `esp_http_client_write` (tampon borné, pas tout en RAM), écrit le trailer,
  lit le code de réponse. Rapporte progression (octets envoyés/total) dans un
  état lisible par l'UI. Libère tout sur tous les chemins.
- **Écran** `main/apps/klipper/ecrans/ecran_usb.{c,h}` (NOUVEL écran) : si non
  monté → « Insérer une clé USB ». Sinon liste 1 colonne scrollable des `.gcode`
  (patron `ecran_fichiers.c`, pagination). Tap → confirmation « Envoyer et
  imprimer <nom> ? » → lance l'upload (barre de progression) → à la réussite,
  message + retour ; à l'échec, message d'erreur. Pendant l'upload, un seul à la
  fois (bouton désactivé).
- **Menu** : ajouter `ECRAN_USB` dans le hub (`ecran_accueil_hub.c` MENU_DEFS,
  ajuster géométrie + `_Static_assert`) OU dans Actions/Configuration — choisir
  le moins invasif (probablement une entrée du hub « USB » ou une case
  Configuration libre). Respecter `NAVIGATION_PROFONDEUR_MAX 4`.

## Contraintes / invariants

- **Rien dans `etat_klipper_t`** (stores USB dédiés).
- **Streaming obligatoire** : jamais le .gcode entier en RAM ni en PSRAM.
- Défense injection d'en-tête sur le filename (guillemets/CRLF).
- Upload sur tâche dédiée : ne bloque ni l'UI ni la tâche WS ; `esp_http_client`
  fermé/libéré sur tous les chemins ; tampon de streaming borné.
- Pièges `*/`, format-truncation sur les fichiers ESP-only.
- USB host tasks (BSP) = 4096 o de pile chacune : ne pas gonfler.

## Tests

- Host-test : Task A (framing multipart + filtre + nettoyage filename).
- idf gate + **matériel** : montage clé, listing .gcode, upload d'un vrai fichier
  vers Moonraker, lancement d'impression, progression, gros fichier (streaming),
  éjection à chaud.

## Hors périmètre

- Pas d'écriture vers la clé, pas de gestion de dossiers USB.
- Pas d'aperçu miniature depuis la clé (les miniatures v1 viennent de Moonraker).
- Pas d'upload sans impression (l'utilisateur a choisi upload+print).
