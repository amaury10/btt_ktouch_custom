# Panneau OTA navigateur + écran propre pendant le flash — Plan d'implémentation

> **Pour agents :** SOUS-COMPÉTENCE REQUISE : superpowers:subagent-driven-development.
> Étapes en cases à cocher (`- [ ]`).

**Goal :** rendre tout le cycle OTA pilotable au navigateur (flash réel avec
progression, sauvegarde/restauration BTT) et éteindre le rétroéclairage pendant
l'écriture flash pour masquer le bruit RGB.

**Architecture :** deux composants, **tous deux dans `firmware/main/web.c`**
(fichier ESP-only). `ota.c` (logique flash validée opus) n'est PAS touché.
Aucune nouvelle route (toutes déjà enregistrées, `max_uri_handlers=16`).

**Tech Stack :** ESP-IDF 5.5.5, esp_http_server, BSP PandaTouch (`pt_backlight_*`).

## Global Constraints

- **Fichier ESP-only** : `web.c` n'est pas compilé en host-test. Validation =
  gate `idf.py build` (contrôleur) + build sim + matériel. Pas de test hôte.
- **Piège `*/`** : ne jamais laisser la séquence `*/` dans un commentaire C d'un
  fichier ESP compilé (referme le bloc → build cassé, invisible host/sim).
- **`-Werror=format-truncation`** : ne dimensionner aucun `snprintf` au plus
  juste ; le gros corps de page passe par un littéral statique sans format.
- **POST du contenu brut** (jamais `<form multipart>`) : le firmware lit le corps
  en octet brut (`httpd_req_recv`), un envelopage multipart casserait le magic 0xE9.
- **Aucune donnée personnelle** dans la page ni les commits.
- `ota.c` reste **inchangé**.

---

### Task 1 : Écran noir pendant l'écriture flash (composant B)

**Files :**
- Modify: `firmware/main/web.c` (include + `gestion_ota_post` + `gestion_restore_btt`)

**Interfaces :**
- Consomme : `pt_backlight_get(void) -> uint32_t`, `pt_backlight_set(uint32_t percent) -> bool`
  (déclarés dans `pandatouch_display.h`).
- Produit : rien de nouveau (comportement inchangé, sauf rétroéclairage éteint
  pendant l'écriture).

- [ ] **Étape 1 : ajouter l'include du BSP display**

Dans `web.c`, à côté des autres `#include "..."` du projet, ajouter :

```c
#include "pandatouch_display.h"
```

- [ ] **Étape 2 : envelopper l'appel commit dans `gestion_ota_post`**

Localiser (web.c ~688) :

```c
    char msg[256];
    esp_err_t resultat = ota_appliquer_flux(req, msg, sizeof(msg));
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
```

Le remplacer par :

```c
    char msg[256];
    /* Ecran noir pendant l'ecriture : un erase/write flash desactive le cache
       flash, ce qui affame le DMA de la dalle RGB (framebuffer en PSRAM) et
       affiche du bruit. pt_backlight_set() ecrit un duty LEDC (registre, rapide,
       maintenu par le peripherique pendant l'ecriture). Sur echec, on ne
       redemarre pas -> restaurer. Sur succes, le chemin plus bas repond puis
       esp_restart() et l'ecran revient au boot. */
    uint32_t retro = pt_backlight_get();
    pt_backlight_set(0);
    esp_err_t resultat = ota_appliquer_flux(req, msg, sizeof(msg));
    if (resultat != ESP_OK) {
        pt_backlight_set(retro);
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
```

- [ ] **Étape 3 : envelopper l'appel restore dans `gestion_restore_btt`**

Localiser (web.c ~779) :

```c
    char msg[256];
    esp_err_t resultat = ota_restaurer_btt(msg, sizeof(msg));
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
```

Le remplacer par :

```c
    char msg[256];
    /* Meme masquage que gestion_ota_post() : ecran noir pendant l'ecriture
       flash de la restauration, restaure sur echec (pas de reboot), reste noir
       sur succes (esp_restart plus bas). */
    uint32_t retro = pt_backlight_get();
    pt_backlight_set(0);
    esp_err_t resultat = ota_restaurer_btt(msg, sizeof(msg));
    if (resultat != ESP_OK) {
        pt_backlight_set(retro);
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
```

- [ ] **Étape 4 : build sim (WSL) pour vérifier la syntaxe C**

Run : `wsl -d Debian -- "/mnt/e/Dev/BTT KTouch Custom/host-test/run.sh"` (ne
compile pas web.c, mais confirme que rien d'autre n'est cassé) puis laisser le
contrôleur lancer le gate `idf.py build`.

- [ ] **Étape 5 : commit**

```bash
git add firmware/main/web.c
git commit -m "feat(ota): ecran noir pendant l'ecriture flash (masque le bruit RGB)"
```

---

### Task 2 : Panneau de contrôle `GET /ota` (composant A)

**Files :**
- Modify: `firmware/main/web.c` (`gestion_ota_page`, web.c:571-633)

**Interfaces :**
- Consomme : `esp_ota_get_running_partition()`, `esp_app_get_description()`,
  `ota_backup_etat()`, `ota_backup_etat_nom()`, `httpd_resp_send_chunk()`,
  `HTTPD_RESP_USE_STRLEN`. Routes appelées côté JS (déjà enregistrées) :
  `POST /ota` (+ `?dry_run=1`), `POST /backup-btt`, `POST /restore-btt`,
  `GET /status`.
- Produit : page HTML panneau à `GET /ota` (remplace la page dry-run seule).

- [ ] **Étape 1 : remplacer intégralement le corps de `gestion_ota_page`**

Remplacer toute la fonction `gestion_ota_page` (de sa ligne `static esp_err_t
gestion_ota_page(httpd_req_t *req)` jusqu'à son `}` fermant, web.c:571-633) par :

```c
static esp_err_t gestion_ota_page(httpd_req_t *req)
{
    const esp_partition_t *courante = esp_ota_get_running_partition();
    const esp_app_desc_t *description = esp_app_get_description();
    const char *backup_btt = ota_backup_etat_nom(ota_backup_etat());

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    /* En-tete : seules les 3 valeurs d'etat passent par snprintf (tampon
       modeste). Le gros corps HTML+JS ci-dessous est un litteral statique sans
       aucun format, envoye tel quel en chunk -- pas de tampon geant, et
       -Werror=format-truncation reste hors de portee sur la partie volumineuse.
       Le corps POSTe le fichier en octet BRUT (File API + XHR/fetch), jamais un
       form multipart, pour rester compatible avec la lecture octet brut cote
       firmware (magic 0xE9 des les premiers octets). */
    char tete[640];
    int n = snprintf(tete, sizeof(tete),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>OTA K-Touch</title></head>"
        "<body style=\"font-family:sans-serif;max-width:34em;margin:1.5em auto;padding:0 1em\">"
        "<h1>Mise a jour OTA</h1>"
        "<p>Slot : <strong id=\"slot\">%s</strong> — version : "
        "<strong id=\"ver\">%s</strong> — sauvegarde BTT : "
        "<strong id=\"bkp\">%s</strong> "
        "<button type=\"button\" onclick=\"rafraichir()\">Rafraichir</button></p>",
        courante != NULL ? courante->label : "?",
        description != NULL ? description->version : "?",
        backup_btt);
    if (n < 0) {
        return httpd_resp_send_500(req);
    }
    esp_err_t e = httpd_resp_send_chunk(req, tete, HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }

    static const char corps[] =
        "<h2>Firmware</h2>"
        "<p>SHA-256 attendu (optionnel) :<br>"
        "<input type=\"text\" id=\"sha\" size=\"66\" placeholder=\"64 hex, optionnel\"></p>"
        "<p>Fichier .bin : <input type=\"file\" id=\"fichier\"></p>"
        "<p><button type=\"button\" onclick=\"verifier()\">Verifier (dry-run)</button> "
        "<button type=\"button\" id=\"btnflash\" onclick=\"flasher()\" disabled>"
        "Flasher (ecriture reelle)</button></p>"
        "<progress id=\"prog\" value=\"0\" max=\"100\" style=\"width:100%;display:none\"></progress>"
        "<pre id=\"res\" style=\"white-space:pre-wrap;background:#eee;padding:.5em;min-height:2em\"></pre>"
        "<h2>Sauvegarde BTT</h2>"
        "<p><button type=\"button\" onclick=\"sauver()\">Sauvegarder BTT vers spiffs</button> "
        "<button type=\"button\" onclick=\"restaurer()\">Restaurer BTT (redemarre sur BTT)</button></p>"
        "<pre id=\"resb\" style=\"white-space:pre-wrap;background:#eee;padding:.5em;min-height:2em\"></pre>"
        "<script>"
        "var okFichier=null;"
        "function elt(i){return document.getElementById(i);}"
        "function majFlash(){elt('btnflash').disabled=(okFichier===null||elt('fichier').files[0]!==okFichier);}"
        "elt('fichier').addEventListener('change',function(){okFichier=null;majFlash();elt('res').textContent='';});"
        "function verifier(){"
        "var f=elt('fichier').files[0];var r=elt('res');"
        "if(!f){r.textContent='choisir un fichier .bin d\\'abord';return;}"
        "var sha=elt('sha').value;"
        "var url='/ota?dry_run=1'+(sha?('&sha='+encodeURIComponent(sha)):'');"
        "r.textContent='verification en cours...';"
        "fetch(url,{method:'POST',body:f}).then(function(resp){return resp.text().then(function(t){"
        "r.textContent='HTTP '+resp.status+'\\n'+t;"
        "if(resp.status===200&&t.indexOf('image valide')>=0){okFichier=f;}majFlash();"
        "});}).catch(function(x){r.textContent='erreur reseau : '+x;});"
        "}"
        "function flasher(){"
        "var f=elt('fichier').files[0];var r=elt('res');"
        "if(!f||f!==okFichier){r.textContent='faire un dry-run valide sur ce fichier d\\'abord';return;}"
        "if(!confirm('Ecriture REELLE dans le slot inactif, puis redemarrage. Continuer ?'))return;"
        "var p=elt('prog');p.style.display='block';p.value=0;"
        "var x=new XMLHttpRequest();"
        "x.upload.onprogress=function(ev){if(ev.lengthComputable){p.value=Math.round(ev.loaded/ev.total*100);}};"
        "x.onload=function(){r.textContent='HTTP '+x.status+'\\n'+x.responseText+"
        "(x.status===200?'\\n(l\\'ecran va noircir puis redemarrer)':'');};"
        "x.onerror=function(){r.textContent='connexion coupee (attendu si l\\'appareil redemarre)';};"
        "x.open('POST','/ota');x.send(f);"
        "}"
        "function sauver(){"
        "if(!confirm('Sauvegarder le firmware BTT (app0) vers spiffs ? Quelques secondes.'))return;"
        "var r=elt('resb');r.textContent='sauvegarde en cours...';"
        "fetch('/backup-btt',{method:'POST'}).then(function(resp){return resp.text().then(function(t){"
        "r.textContent='HTTP '+resp.status+'\\n'+t;rafraichir();"
        "});}).catch(function(x){r.textContent='erreur reseau : '+x;});"
        "}"
        "function restaurer(){"
        "if(!confirm('RESTAURER BTT : reecrit le slot inactif et REDEMARRE la dalle sur le firmware BTT. Continuer ?'))return;"
        "var r=elt('resb');r.textContent='restauration en cours...';"
        "fetch('/restore-btt',{method:'POST'}).then(function(resp){return resp.text().then(function(t){"
        "r.textContent='HTTP '+resp.status+'\\n'+t;"
        "});}).catch(function(x){r.textContent='connexion coupee (attendu si l\\'appareil redemarre)';});"
        "}"
        "function rafraichir(){"
        "fetch('/status').then(function(r){return r.json();}).then(function(j){"
        "elt('slot').textContent=j.slot;elt('ver').textContent=j.version;elt('bkp').textContent=j.backup_btt;"
        "}).catch(function(){});"
        "}"
        "</script>"
        "</body></html>";
    e = httpd_resp_send_chunk(req, corps, HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}
```

- [ ] **Étape 2 : mettre à jour l'en-tête de fichier / le lien de la page d'accueil**

Le commentaire de tête de `gestion_ota_page` (web.c:559-570) décrit l'ancienne
page « dry-run seulement » — le réécrire pour dire « panneau : etat + dry-run +
flash reel gardé par dry-run + boutons backup/restore ». Vérifier qu'aucun `*/`
parasite n'y est introduit. La ligne d'accueil (`GET /`, web.c:139) mentionnant
`/ota` peut rester ; l'ajuster si elle dit encore « ecriture reelle en POST
direct » pour refléter le panneau.

- [ ] **Étape 3 : build sim (WSL)**

Run : `wsl -d Debian -- "/mnt/e/Dev/BTT KTouch Custom/host-test/run.sh"` (confirme
que le reste du projet compile ; web.c est gaté par idf côté contrôleur).

- [ ] **Étape 4 : commit**

```bash
git add firmware/main/web.c
git commit -m "feat(ota): panneau navigateur (flash reel garde par dry-run + backup/restore)"
```

---

## Self-Review (contrôleur, après écriture du plan)

- Couverture spec : Task 1 = composant B (écran noir) ; Task 2 = composant A
  (panneau). ✅
- Pas de placeholder : code complet fourni pour chaque édition. ✅
- Cohérence types : `pt_backlight_get/set` (uint32_t/bool), `httpd_resp_send_chunk`
  + `HTTPD_RESP_USE_STRLEN`, ids DOM (`slot/ver/bkp/sha/fichier/btnflash/prog/
  res/resb`) cohérents entre HTML et JS. ✅
- Pièges ESP : `*/` (aucun dans le code fourni), format-truncation (corps en
  littéral statique). Gate idf obligatoire après CHAQUE tâche. ✅
