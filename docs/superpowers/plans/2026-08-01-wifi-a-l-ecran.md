# Réglages WiFi à l'écran — Plan d'implémentation

> **Pour agents :** SOUS-SKILL REQUISE : superpowers:subagent-driven-development.

**But :** écran de réglages WiFi (scan + liste + mot de passe) qui persiste
sûrement les identifiants dans notre NVS, + fix du multi-reboot au démarrage.

**Architecture :** stockage dans l'espace NVS `ktouch` (`reglages`, jamais la
NVS WiFi partagée) ; `wifi.c` lit nos identifiants en priorité, scanne, et
reconfigure à chaud avec « essayer-puis-persister » ; écran modèle
`ecran_macros.c` ; fix sauvetage = désarmer une fois le firmware sain.

**Pile :** C, ESP-IDF, LVGL ; host-test partiel (le cœur WiFi est matériel).

## Contraintes globales

- **Ne JAMAIS écrire la NVS WiFi d'ESP** (`WIFI_STORAGE_RAM` conservé) ni
  `nvs_flash_erase` : la partition nvs est partagée avec le fw d'origine.
  Identifiants WiFi rangés dans l'espace `ktouch` (reglages) uniquement.
- **Anti-verrouillage** : ne persister de nouveaux identifiants qu'APRÈS un
  `GOT_IP` réussi ; sinon restaurer les précédents. Un mauvais mot de passe ne
  doit jamais rendre l'appareil injoignable au reboot.
- Réseau-libre dans les callbacks LVGL (scan/reconfigure via appels non
  bloquants ou tâche dédiée, jamais `vTaskDelay` dans un rappel LVGL).
- FR ; thème sombre ; cibles ≥44px ; aucune donnée personnelle (surtout : aucun
  SSID/mot de passe RÉEL dans un commit — valeurs de test factices only).

---

## Task 1 : Fix du sauvetage (désarmer une fois le firmware sain)

**Files :** Modify `firmware/main/app_main.c` ; commentaires `wifi.c`.

- [ ] **Step 1** — Dans `app_main`, APRÈS que l'écran + l'UI + la boucle sont
  debout (le point où le firmware a passé toutes les étapes risquées — juste
  avant/au début de la boucle applicative principale, après `boucle_*`/le
  premier `habillage_pomper` et la création du timer d'interface), ajouter
  `rescue_disarm(); rescue_reset_boot_count();`. Repérer ce point en lisant
  `app_main.c` en entier (la séquence : nvs → wifi_start → web_start → reglages →
  backend/boucle → écran/UI → boucle principale).
- [ ] **Step 2** — Mettre à jour les commentaires devenus faux :
  `app_main.c:299-302` (« si le WiFi ne se connecte jamais, rescue_disarm() n'est
  jamais appelé ») et `wifi.c` (§ qui dit que seul `GOT_IP` désarme) — expliquer
  le nouveau contrat : le sauvetage couvre les étages d'init risqués ; une fois
  le firmware prouvé sain, on désarme et le WiFi retente sans rebooter ; le
  compteur RTC couvre toujours les crashs. Documenter le compromis (un firmware
  sain qui ne joindra jamais le WiFi ne se rebascule plus — corrigé à l'écran,
  sous-projet WiFi).
- [ ] **Step 3 — build firmware** (`idf.py build`) propre ; host-test vert
  (aucune régression). (Le comportement runtime est matériel : validé à l'écran.)
- [ ] **Step 4 — commit** : `git commit -am "fix(rescue): desarmer le sauvetage une fois le firmware sain (fin du multi-reboot au demarrage)"`.

---

## Task 2 : Stockage WiFi dans reglages + lecture-prioritaire

**Files :** Modify `firmware/main/core/reglages.{h,c}`, `firmware/main/wifi.c` ;
Test `host-test/tests/test_reglages*` (si présent/host-testable).

**Interfaces :**
- Produces : `bool reglages_wifi(char *ssid, size_t taille_ssid, char *pass, size_t taille_pass);`
  (rend faux si aucun SSID saisi) ; `esp_err_t reglages_definir_wifi(const char *ssid, const char *pass);`

- [ ] **Step 1** — `reglages.c` : clés `wifi_ssid`/`wifi_pass` dans l'espace
  `ktouch` (modèle : l'hôte). SSID ≤ 32, mot de passe ≤ 63 (+NUL). Ne persiste
  jamais un SSID vide. Défauts : vide (→ faux).
- [ ] **Step 2** — `wifi.c` : dans la sélection des identifiants (voir la
  logique NVS/Kconfig existante), lire `reglages_wifi()` EN PREMIER ; s'il rend
  vrai, l'appliquer (RAM) ; sinon retomber sur Kconfig puis NVS héritée
  (ordre actuel inchangé pour le repli). `WIFI_STORAGE_RAM` conservé.
- [ ] **Step 3** — tests : si `test_reglages` (ou équivalent host) existe et que
  la NVS est shimée en host, couvrir écriture/lecture/rejet SSID vide. Sinon,
  signaler que c'est firmware-only et s'appuyer sur le build.
- [ ] **Step 4 — build** host-test + firmware verts.
- [ ] **Step 5 — commit** : `git commit -am "feat(wifi): identifiants WiFi dans reglages (NVS ktouch), lus en priorite"`.

---

## Task 3 : API WiFi scan + reconfiguration (essayer-puis-persister) + état

**Files :** Modify `firmware/main/wifi.{h,c}`.

**Interfaces :**
- Produces :
  - `typedef struct { char ssid[33]; int8_t rssi; bool chiffre; } wifi_reseau_t;`
  - `esp_err_t wifi_scanner(wifi_reseau_t *sortie, size_t max, size_t *nb);` (dédup par SSID, tri RSSI desc)
  - `esp_err_t wifi_reconfigurer(const char *ssid, const char *pass);`
  - `void wifi_etat(char *ssid_sortie, size_t taille, bool *connecte);`

- [ ] **Step 1** — `wifi_scanner` : `esp_wifi_scan_start` bloquant borné,
  récupérer les AP, dédupliquer par SSID (garder le meilleur RSSI), trier RSSI
  décroissant, remplir jusqu'à `max`. `chiffre` = authmode ≠ OPEN.
- [ ] **Step 2** — `wifi_reconfigurer` : sauvegarder une COPIE des identifiants
  courants (RAM) ; appliquer les nouveaux (RAM, `esp_wifi_set_config`) ;
  `esp_wifi_disconnect()` + `esp_wifi_connect()`. Mémoriser « reconfiguration en
  attente + identifiants candidats ». Dans le handler `GOT_IP` (wifi.c) : si une
  reconfiguration est en attente et réussit → `reglages_definir_wifi(candidats)`
  (persiste) + effacer l'attente. Dans le handler d'échec/déconnexion : après N
  tentatives de la reconfiguration candidate → restaurer la COPIE précédente
  (RAM) + `esp_wifi_connect()` + effacer l'attente + marquer « échec » pour l'UI.
  AUCUNE persistance sur échec.
- [ ] **Step 3** — `wifi_etat` : SSID courant (`ssid_utilise`) + connecté
  (drapeau maintenu par GOT_IP/DISCONNECT). Exposer aussi un état de la dernière
  reconfiguration (en cours / réussie / échouée) pour le retour écran — via un
  getter simple (`wifi_reconfig_etat()`), non bloquant.
- [ ] **Step 4 — build firmware** propre. (Runtime = matériel : non host-testable ;
  signaler. La sûreté repose sur « persiste seulement sur GOT_IP ».)
- [ ] **Step 5 — commit** : `git commit -am "feat(wifi): scan + reconfiguration a chaud (essayer-puis-persister) + etat"`.

---

## Task 4 : Écran Réglages WiFi + câblage depuis la config

**Files :** Create `firmware/main/apps/klipper/ecrans/ecran_reglages_wifi.{h,c}` ;
Modify `ecran_configuration.c` (bouton « WiFi ») ; les 4 CMake/`main.c` ;
Test `host-test/tests/test_ecran_reglages_wifi.c`.

**Interfaces :**
- Consumes : `wifi_scanner`/`wifi_reconfigurer`/`wifi_etat` (T3), `clavier.h`,
  `navigation_empiler`. Produces : `extern const ecran_desc_t ECRAN_REGLAGES_WIFI;` (id `"wifi"`).

- [ ] **Step 1 — test qui échoue** (`test_ecran_reglages_wifi.c`, modèle
  `test_ecran_macros.c`) : avec une façade `wifi_*` mockée en host (retourne 2-3
  réseaux factices), l'écran liste les SSID ; tap sur `"MonReseau"` → clavier
  ouvert (titre = SSID) ; valider `"motdepasse"` → `wifi_reconfigurer` appelé
  avec `("MonReseau","motdepasse")` (tracé via la façade mock) ; réseau ouvert →
  pas de clavier, `wifi_reconfigurer(ssid,"")` direct ; grisage ; en-tête montre
  `wifi_etat`. (Pour rendre `wifi_*` mockable en host, mettre la façade derrière
  un petit en-tête que le host-test peut fournir en double — voir comment
  `plateforme_*` est shimé côté host.)
- [ ] **Step 2 — voir échouer**.
- [ ] **Step 3 — implémenter** l'écran (scan à l'ouverture + « Recherche… » ;
  liste SSID + barres RSSI + cadenas ; tap → clavier mot de passe →
  `wifi_reconfigurer` ; en-tête `wifi_etat` ; retour « Connexion… / Connecté /
  Échec » via `mettre_a_jour` + l'état de reconfiguration ; bouton « Rescanner ») ;
  câbler un bouton « WiFi » dans `ecran_configuration.c` →
  `navigation_empiler(&ECRAN_REGLAGES_WIFI)` ; les 4 CMake/`main.c` + `--ecran wifi`
  dans le sim (avec la façade factice).
- [ ] **Step 4 — voir passer** (host-test vert) + build firmware + capture sim.
- [ ] **Step 5 — commit** : `git commit -am "feat(ui): ecran reglages WiFi (scan + liste + mot de passe)"`.

## Self-Review

- **Couverture spec** : fix sauvetage (T1), stockage+lecture (T2), scan+reconfig
  sûre (T3), écran+câblage (T4). ✓
- **Anti-verrouillage** : persiste seulement sur GOT_IP (T3) ; sauvetage désarmé
  (T1) → mauvais mot de passe = pas de rebascule, corrigé à l'écran. ✓
- **NVS partagée protégée** : écriture uniquement dans `ktouch`, `WIFI_STORAGE_RAM`
  conservé (T2). ✓
- **Cohérence types** : `wifi_scanner`/`wifi_reconfigurer`/`wifi_etat`/`wifi_reseau_t`
  T3↔T4 ; `reglages_wifi`/`reglages_definir_wifi` T2↔T3 ; `ECRAN_REGLAGES_WIFI`
  id `"wifi"`. ✓
