# Réglages WiFi à l'écran + fix du sauvetage — sous-projet 7

Date : 2026-08-01
Statut : design (demande utilisateur ; saisie choisie = scan + liste + mot de
passe ; à relire — touche le WiFi, matériel-critique).

## 1. Contexte & objectif

Aujourd'hui les identifiants WiFi viennent soit de **Kconfig** (compilés en dur),
soit de la **NVS héritée** du firmware d'origine (`wifi.c`, qui met
délibérément `esp_wifi_set_storage(WIFI_STORAGE_RAM)` pour ne JAMAIS écrire la
NVS WiFi partagée). Il n'existe **aucune** saisie WiFi dans notre interface.

Objectif : un écran **Réglages WiFi** qui **scanne** les réseaux, les liste, et
permet d'en choisir un + saisir le mot de passe → connexion + persistance. Et
corriger le **multi-reboot au démarrage** (le sauvetage rebascule quand le WiFi
n'a pas d'IP en 90 s — or à froid le routeur/l'imprimante n'est pas encore là).

## 2. Contrainte NVS (le point critique)

La partition `nvs` (0x9000) est **partagée** avec le firmware d'origine, qui y
garde ses identifiants WiFi. On ne DOIT jamais écrire la NVS WiFi d'ESP (ni
`nvs_flash_erase`). Donc : on stocke SSID+mot de passe dans **notre** espace de
noms NVS `ktouch` (celui des `reglages`, qu'on écrit déjà pour l'hôte), et
`wifi.c` les applique en RAM (`WIFI_STORAGE_RAM` conservé). La NVS de l'origine
reste intacte.

## 3. Garde-fou anti-verrouillage (essayer-puis-persister)

Un mauvais mot de passe saisi à l'écran ne doit JAMAIS rendre l'appareil
injoignable après reboot :
1. On applique les nouveaux identifiants **en RAM** et on tente la connexion.
2. **Seulement sur `IP_EVENT_STA_GOT_IP`** (connexion réussie) on les
   **persiste** dans `reglages` (NVS `ktouch`).
3. En cas d'échec (timeout / auth refusée), on **restaure** les identifiants
   précédents (ceux qui marchaient) en RAM et on **ne persiste rien**.
Ainsi un reboot relit toujours les derniers identifiants qui ont RÉELLEMENT
connecté. Combiné au fix du sauvetage (§6), un mauvais mot de passe se corrige
tranquillement à l'écran, sans rebascule ni lockout.

## 4. Priorité des identifiants au boot (`wifi.c`)

`reglages` (saisis à l'écran) **>** Kconfig **>** NVS héritée (nettoyée du
BSSID). C'est le seul changement d'ordre : si l'utilisateur a saisi un réseau à
l'écran, il prime.

## 5. Composants

### 5.1 Stockage (`reglages.{h,c}`)
`reglages_wifi(char *ssid, size_t, char *pass, size_t)` (lecture, rend faux si
rien de saisi) + `reglages_definir_wifi(const char *ssid, const char *pass)`
(écriture, espace `ktouch`). SSID ≤ 32, mot de passe ≤ 63 (+NUL). Ne persiste
JAMAIS un SSID vide.

### 5.2 WiFi (`wifi.c`)
- Lecture-prioritaire des identifiants `reglages` au démarrage (§4).
- `wifi_scanner(wifi_reseau_t *sortie, size_t max, size_t *nb)` : scan bloquant
  borné, rend la liste {SSID, RSSI, chiffré?} dédupliquée par SSID, triée par
  RSSI. (Scan en mode STA connecté : supporté par esp_wifi ; brève interruption
  tolérée.)
- `wifi_reconfigurer(const char *ssid, const char *pass)` : garde une COPIE des
  identifiants courants, applique les nouveaux en RAM, `esp_wifi_disconnect()` +
  `esp_wifi_connect()`. Le handler `GOT_IP` (déjà présent) persiste via
  `reglages_definir_wifi` si la connexion vient d'un `wifi_reconfigurer` en
  attente ; un handler d'échec (N tentatives) restaure la copie précédente.
- `wifi_etat(char *ssid_sortie, size_t, bool *connecte)` : SSID courant + état,
  pour l'affichage.

### 5.3 Écran (`ecran_reglages_wifi.{h,c}`)
Atteint depuis l'écran de configuration existant (`ecran_configuration.c`, ouvert
par l'engrenage de la barre) : un bouton « WiFi » y empile ce nouvel écran.
- À l'ouverture : lance un scan (indicateur « Recherche… »), puis liste les
  réseaux (SSID + barres de signal + cadenas si chiffré), modèle grille/liste de
  `ecran_macros.c`. En-tête : « Actuel : \<SSID\> (connecté/…) » depuis `wifi_etat`.
- Tap sur un réseau → `clavier_ouvrir(SSID, "", CLAVIER_TEXTE, rappel, …)` pour
  le mot de passe → `wifi_reconfigurer(ssid, pass)`. Réseau ouvert (non chiffré)
  → pas de clavier, reconfigure directement.
- Retour visuel : « Connexion… » puis « Connecté à \<SSID\> » (persisté) ou
  « Échec — vérifie le mot de passe » (anciens identifiants restaurés) via
  `mettre_a_jour`/`wifi_etat`. Bouton « Rescanner ».

## 6. Fix du sauvetage (bundlé)

`app_main.c` : le sauvetage est armé avant l'écran/WiFi (inchangé, il couvre les
étages risqués). On AJOUTE, une fois l'écran + l'UI + la boucle debout (preuve
que le firmware tourne, indépendamment du WiFi) : `rescue_disarm()` +
`rescue_reset_boot_count()`. Un firmware sain qui attend un réseau absent ne
rebascule donc plus (le WiFi retente indéfiniment sans reboot) ; le filet couvre
toujours un vrai mauvais flash (qui échoue AVANT ce point, ou le compteur RTC
l'attrape sur crash). Mettre à jour les commentaires de `wifi.c:299`/`app_main`
qui affirment que seul `GOT_IP` désarme. C'est ce qui rend §3 pleinement sûr :
mauvais mot de passe = pas de rebascule, correction à l'écran.

## 7. Gestion d'erreurs

Scan en échec → « Aucun réseau / réessaie ». Reconfiguration en échec → anciens
identifiants restaurés + message. Backend/UI restent réseau-libres dans les
callbacks LVGL (le scan/reconfigure passent par des appels non bloquants ou une
tâche dédiée, jamais un `vTaskDelay` dans un rappel LVGL).

## 8. Tests

- Host-testable : la logique `reglages` WiFi (stockage/lecture/défauts, si le
  shim NVS host le permet — sinon inspection) ; l'écran (`test_ecran_reglages_wifi.c`)
  avec une liste de réseaux simulée + un `wifi_*` mocké (scan/reconfigure/etat
  derrière une petite façade mockable en host) : liste peuplée, tap → clavier →
  appel `wifi_reconfigurer` avec le bon SSID/mot de passe, grisage.
- NON host-testable (matériel) : le scan réel, la reconnexion, le
  `GOT_IP`→persiste / échec→restaure, le désarmement du sauvetage. Validés à
  l'écran par l'utilisateur. Le garde-fou §3 rend l'échec non destructif.

## 9. Périmètre & hors-scope

- **Dans** : scan + liste + mot de passe, persistance sûre, lecture-prioritaire,
  reconnexion à chaud, fix du sauvetage.
- **Hors** : réseaux cachés (saisie manuelle du SSID — suivi possible), WPA2-
  entreprise, oubli d'un réseau enregistré, plusieurs réseaux mémorisés (un seul
  jeu d'identifiants courant), portail captif.
