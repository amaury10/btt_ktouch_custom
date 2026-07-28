# Flasher et récupérer la K-Touch sans câble série

## Contexte : ce qui a été perdu, et pourquoi

Le filet de sécurité prévu à l'origine pour ce projet était simple : dumper les
16 Mio de flash avant toute manipulation, vérifier la sauvegarde
(`python ktouch-cli.py verify`, voir `docs/hardware/partitions.md`), et pouvoir
la restaurer octet par octet en cas de problème.

**Ce filet n'existe pas dans ce montage.** Le port USB-C de la K-Touch utilisée
ici ne sert qu'à l'alimentation — aucun port série ne s'énumère derrière —,
donc `esptool` est hors jeu et aucune sauvegarde de la flash n'est possible.
Concrètement :

- **Ce qui reste récupérable** : le firmware d'origine BIGTREETECH, intact dans
  son slot OTA (`app0`, jamais réécrit par ce projet), et les images
  officielles publiées par BTT (`K-Touch_v1.1.0_partition.bin` et consorts,
  disponibles sur leur dépôt GitHub). Le firmware custom de ce dépôt peut
  toujours être reconstruit et réinstallé.
- **Ce qui n'est PAS récupérable en cas de perte** : la NVS (partition `nvs`,
  0x9000-0xDFFF). Si elle est effacée ou corrompue, les réglages de l'appareil
  et les identifiants WiFi saisis sur l'écran par l'utilisateur final sont
  perdus — ce projet n'en a aucune copie et ne peut pas les reconstituer.

Le remplacement du filet, c'est le firmware lui-même : voir
`firmware/main/rescue.c`, `wifi.c`, `netlog.c` et `web.c`. Ce document décrit
comment s'en servir.

## Installer le firmware sur l'appareil

L'installation passe par le mécanisme OTA du **firmware d'origine**, qui écrit
dans le slot inactif et bascule le démarrage lui-même. C'est la voie la plus
sûre : on ne manipule aucun offset à la main.

### 1. Vérifier l'état de départ

```powershell
curl.exe -s http://<ip-de-la-k-touch>/update/identity
```

Attendu : un JSON du type `{"id": "V1.0.0", "hardware": "ESP32"}`. Noter cette
version — c'est ce à quoi l'appareil doit revenir ensuite.

> Si cette requête échoue, **s'arrêter**. Un appareil déjà injoignable avant
> toute écriture ne doit pas en recevoir une.

### 2. Vérifier que le binaire est le bon

```powershell
python ktouch-cli.py image firmware/build/ktouch-custom.bin
```

Attendu : `puce : ESP32-S3`, `projet : ktouch-custom`, et une taille très
inférieure aux 4 718 592 octets du slot.

> **N'installer qu'un firmware capable de revenir en arrière.** Un firmware sans
> pile réseau — une simple mire, par exemple — n'a aucun moyen de reprendre la
> main une fois démarré, et sans accès série c'est un aller sans retour.

### 3. Téléverser

Ouvrir `http://<ip-de-la-k-touch>/update` dans un navigateur et sélectionner
`firmware/build/ktouch-custom.bin`. La page annonce la fin du téléversement puis
le redémarrage.

> **Un refus est sans conséquence.** Si l'OTA d'origine rejette l'image, rien
> n'a été écrit et l'appareil continue de tourner normalement. Ce n'est pas un
> échec, c'est une information — et il ne faut pas chercher à contourner le
> contrôle.

### 4. Constater

Laisser une minute, puis lire l'écran. Depuis la tâche 10 du sous-jalon 2b,
l'écran affiche l'interface Klipper réelle par-dessus la mire de diagnostic du
jalon 1, pas la mire elle-même :

- **Appareil jamais configuré (aucun hôte Moonraker enregistré)** : écran
  « Settings » (`ecran_configuration.c`) empilé par-dessus l'écran d'accueil —
  champs « Printer address » et « Machine type », valeur « Not configured »
  tant que rien n'a été saisi, bouton « Save ». C'est l'état normal d'un
  premier démarrage, pas une panne.
- **Appareil déjà configuré** : écran d'accueil Klipper directement (tuiles de
  température, progression d'impression, etc.), sans l'écran de configuration
  par-dessus.
- **Écran ou tactile en panne** (`pt_display_init()` en échec, ou GT911
  muet) : l'appareil reste diagnosticable à distance (WiFi, `/log`, `/state`,
  `/revert`) même sans rien afficher — voir le commentaire en tête
  d'`app_main()`.

La ligne d'état de la mire du jalon 1 (slot, compteur de démarrages, source
des identifiants WiFi, adresse IP) n'est plus visible dans le cas normal :
l'habillage (bande d'état 44 px) et le fond opaque de chaque écran empilé la
recouvrent entièrement. Elle ne refait surface que si l'empilement de l'écran
de départ a lui-même échoué (voir les `JOURNAL_ERREUR` correspondants dans
`app_main.c`) — un repli dégradé mais lisible, pas un défaut à corriger.

Puis, à distance :

```powershell
curl.exe -s http://<ip-de-la-k-touch>/status
curl.exe -s http://<ip-de-la-k-touch>/state
curl.exe -s http://<ip-de-la-k-touch>/log
```

`/state` (voir le tableau de routes plus bas) reste disponible exactement
comme avant, EN PARALLÈLE de l'interface graphique : l'écran est une
présentation visuelle du même état que celui exposé en JSON, pas un canal
séparé — les deux peuvent être consultés indépendamment, y compris quand
l'écran est en panne. `/revert` n'est pas affecté par l'arrivée de
l'interface : il continue de basculer immédiatement sur le firmware d'origine
quel que soit l'écran affiché au moment de l'appel.

> Si rien ne répond au bout de deux minutes, **ne rien faire** : le sauvetage
> automatique décrit plus bas ramène l'appareil au firmware d'origine tout seul.
> Attendre, puis le vérifier avec `curl.exe -s http://<ip>/update/identity`.

## Les trois mécanismes, du plus manuel au plus automatique

| Mécanisme | Déclencheur | Dépend de |
|---|---|---|
| Retour manuel (`/revert`) | requête HTTP volontaire | WiFi fonctionnel |
| Sauvetage automatique (minuteur) | absence de connexion WiFi à l'échéance | rien — ni écran, ni tactile, ni réseau |
| Sauvetage automatique (compteur de démarrages) | plus de `RESCUE_DEMARRAGES_MAX` redémarrages consécutifs | rien — survit même à une panique ou un chien de garde |

**Ce firmware n'écrit jamais dans une partition applicative, ni `app0` ni
`app1`.** La seule écriture flash de tout le firmware est celle d'`otadata`
(8 Kio), qui désigne le slot de démarrage — dans `rescue.c`, en dernier
recours seulement, si le bootloader refuse la bascule normale.

## Pourquoi il n'y a pas de route `/update` sur ce firmware

C'est contre-intuitif, et c'est le point que la conception initiale de ce
document avait faux : **l'itération sur le pinout ne passe pas par notre
firmware**, mais toujours par celui d'origine.

Ce firmware custom tourne depuis `app1` — c'est le slot que l'OTA du firmware
d'origine choisit pour lui. Avec seulement deux slots OTA, le « slot inactif »
vu depuis `app1` est donc `app0`, celui du firmware d'origine. Un `/update`
sur notre firmware n'aurait nulle part ailleurs où écrire, et
`esp_ota_begin(OTA_SIZE_UNKNOWN)` efface la partition cible **avant** de
recevoir le moindre octet : la première mise à jour effacerait donc le
firmware d'origine lui-même, après quoi le sauvetage n'aurait plus rien vers
quoi basculer. C'est pourquoi ce firmware n'expose aucune route de mise à
jour, et pourquoi `web.c` ne contient aucun `esp_ota_begin`/`esp_ota_write`.

## Routes HTTP exposées

Le serveur écoute sur le port 80, à l'adresse IP journalisée au démarrage
(`adresse IP : ...` dans les logs, et rapportée dans `/status`).

| Route | Méthode | Rôle |
|---|---|---|
| `/` | GET | page d'état minimale, avec liens vers les autres routes |
| `/status` | GET | JSON : slot en cours, version, adresse IP, temps depuis le démarrage, mémoire libre, tactile disponible ou non, compteur de démarrages |
| `/state` | GET | JSON : état de la liaison avec l'hôte Klipper, génération de l'état, et le dernier état connu — `extrudeurs` (jusqu'à 8, tableau des seuls présents avec leur `index` d'origine), `nb_extrudeurs`, `outil_actif`, `plateau`, `ventilateurs`, position XYZ et axes référencés, vitesse/flux en %, décalage Z (babystep), `macros` (tableau de noms) et `macros_tronquees`, fichier, progression, temps restant, impression en cours/en pause |
| `/log` | GET | texte brut, contenu du journal réseau en RAM (dernières lignes de log) |
| `/revert` | POST | bascule vers l'autre slot OTA et redémarre |

`/revert` est en **POST** délibérément : en GET, n'importe quelle requête d'un
navigateur, d'un aspirateur de liens ou d'un scanner réseau redémarrerait
l'appareil.

Le compteur de démarrages rapporté par `/status` est un instantané pris une
seule fois au démarrage (juste après `rescue_count_boot()`), pas une valeur
relue en direct : il ne change pas entre deux requêtes sur un même
démarrage, même si une connexion WiFi réussit entre-temps et le remet à zéro
en coulisse pour le prochain démarrage.

Dans la réponse de `/state`, `"generation":0` signifie précisément « aucun
relevé n'a encore été validé » — hôte non configuré, boucle pas encore
démarrée, démarrée mais aucun cycle réussi depuis, ou hôte configuré mais
injoignable (Moonraker down : `boucle_demarrer()` réussit dès que
`demarrer()` du backend réussit — pour Moonraker, cela ne fait que créer un
client HTTP, sans contacter la machine, voir `backend_moonraker.c`). C'est le
seul signal qui distingue cet état de « la machine existe et tous ses champs
valent authentiquement zéro » : tant que `generation` vaut 0, `"etat":null`
dans la même réponse et rien sous cette clé ne doit être interprété comme une
lecture réelle de la machine. Une fois qu'un premier cycle a réussi,
`generation` avance à chaque nouveau relevé validé (voir `boucle_generation()`
dans `firmware/main/core/boucle.h`) et `etat` cesse d'être `null`.

**Zéro honnête, indépendamment de `generation`.** Certains champs de `etat`
(v2, jalon 3a) ne sont pas encore renseignés par le seul GET HTTP
Moonraker actuellement branché : `position`, `vitesse_pct`, `flux_pct`,
`babystep_z_um`, `macros`/`nb_macros`/`macros_tronquees`. Tant que la
souscription WebSocket qui les remplira n'est pas câblée (tâches à venir de
ce même jalon), ces champs valent `0`/`[]`/`false` — pas parce que la
machine vaut authentiquement zéro sur ces grandeurs, mais parce que rien ne
les a encore mesurées. Même prudence que pour `generation` : un client qui
lit `"vitesse_pct":0` ne doit pas l'afficher comme « vitesse à l'arrêt »
tant qu'il n'a pas d'autre moyen de distinguer « jamais reçu » de
« mesuré à zéro » sur ce champ précis (voir le commentaire de chaque champ
dans `firmware/main/core/etat_klipper.h`, qui documente ce que sa valeur
zéro signifie). `extrudeurs`/`plateau`, eux, portent directement leur
propre drapeau `presente` : c'est le signal explicite à lire pour eux,
plutôt qu'une convention implicite sur la valeur zéro.

**`generation` n'est PAS un signal de vivacité de la boucle.** Il n'avance
QUE lorsque le contenu de l'état change réellement d'un cycle à l'autre
(comparaison mémoire dans `etat_store_valider()`) — une imprimante au repos,
dont chaque relevé Klipper est identique au précédent, laisse `generation`
figée indéfiniment alors même que la boucle interroge Moonraker avec succès
une fois par seconde. Pour savoir si la boucle est en train de fonctionner,
c'est le champ `liaison` qu'il faut lire : `"en ligne"` signifie que le
dernier cycle a réussi (que son contenu ait changé ou non), `"degradee"` ou
`"hors ligne"` signalent des échecs consécutifs, et `"connexion"` veut dire
qu'aucun cycle n'a encore abouti depuis le démarrage. `generation` répond à
« l'affichage doit-il se redessiner ? », `liaison` répond à « la liaison
fonctionne-t-elle en ce moment ? » — deux questions différentes, à ne pas
confondre.

## Revenir au firmware d'origine (le WiFi marche, l'affichage est raté)

C'est le cas couvert par `/revert` : le firmware custom a démarré, le WiFi a
joint le réseau (donc le sauvetage automatique s'est désarmé), mais l'écran ou
le tactile ne fonctionnent pas comme attendu. Comme le firmware d'origine
n'est jamais écrasé dans son slot, revenir au stock ne demande aucun
téléversement :

```bash
curl.exe -X POST http://<ip-de-la-k-touch>/revert
```

L'appareil bascule immédiatement sur l'autre slot (`app0`, le firmware
d'origine) et redémarre dessus.

## Sauvetage automatique (le WiFi ne répond pas du tout, ou l'appareil boucle)

Deux mécanismes indépendants, qui ne dépendent ni du réseau, ni de l'écran, ni
du tactile :

**Le minuteur** couvre les pannes plus lentes que son échéance
(`CONFIG_KTOUCH_RESCUE_TIMEOUT_MS`, 90 secondes par défaut). Armé tout au
début d'`app_main`, avant l'écran, avant le WiFi, il ne dépend que de
lui-même. Si le WiFi n'est pas connecté à son échéance — mauvais SSID, mot de
passe incorrect, réseau hors de portée —, le firmware bascule seul la
partition de démarrage sur l'autre slot et redémarre.

Le seul endroit du firmware qui désarme ce minuteur est le gestionnaire de
`IP_EVENT_STA_GOT_IP` dans `wifi.c`, c'est-à-dire une connexion WiFi
effectivement réussie (adresse IP obtenue). Rien d'autre ne le désarme.

**Le compteur de démarrages** couvre presque tout ce que le minuteur ne voit
pas : une panne plus rapide que 90 secondes — panique, chien de garde,
débordement de pile — **à condition qu'elle survienne après l'entrée dans
`app_main`**. Il vit en mémoire RTC (`RTC_NOINIT_ATTR`), donc survit à un
redémarrage logiciel comme à une panique, mais pas à une coupure
d'alimentation. Incrémenté tout au début d'`app_main`, avant même le
minuteur, il bascule immédiatement l'appareil sur l'autre slot dès qu'il
dépasse trois démarrages consécutifs sans qu'une adresse IP n'ait jamais été
obtenue — après avoir d'abord remis lui-même le compteur à zéro (sinon,
retenter d'installer ce même firmware par-dessus repartirait déjà au-delà du
seuil et basculerait sans jamais tenter de démarrer). Une connexion WiFi
réussie le remet à zéro, au même endroit que le minuteur.

Dans les deux cas, aucune intervention n'est nécessaire : il suffit d'attendre
que l'appareil se rétablisse de lui-même.

**Ce que ni le minuteur ni le compteur ne couvrent : un échec de la PSRAM.**
`esp_psram_chip_init()` tourne depuis `cpu_start.c`, avant l'ordonnanceur et
avant `app_main` : un échec y déclenche un `abort()` alors qu'aucune ligne du
code de secours n'a encore eu la moindre chance de s'exécuter — ni compteur,
ni minuteur, ni serveur HTTP. Comme `otadata` désigne toujours ce même slot,
une coupure de courant ne change rien : l'appareil recommencerait
indéfiniment. C'est la seule classe de pannes qui rendrait l'appareil
définitivement injoignable, et elle ne se traite pas dans le code
(impossible : rien n'a encore tourné) mais en amont, dans
`firmware/sdkconfig.defaults`. `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` y est
délibérément activé pour que ce jalon soit exactement le terrain où cette
question doit se poser : une PSRAM absente ou incompatible avec la K-Touch
5 pouces (les réglages de départ viennent du BSP du Panda Touch 7 pouces)
n'abat plus le démarrage — elle fait simplement échouer l'allocation du
tampon LVGL dans le BSP, donc `pt_display_init()` rend une erreur, et l'on
retombe dans le cas déjà géré plus haut : écran mort, WiFi et `/revert` bien
vivants. Le code et les constantes ne sont plus placés en PSRAM non plus
(`CONFIG_SPIRAM_FETCH_INSTRUCTIONS`/`CONFIG_SPIRAM_RODATA` retirés), pour
qu'un timing marginal se traduise par un échec d'allocation propre plutôt que
par un plantage sur une lecture d'instruction. Ce n'est pas gratuit : ce
sont des marges qu'une preuve de vie n'a pas besoin d'exploiter.

## Itérer sur le pinout (le cas d'usage principal de ce jalon)

Corriger un pinout mal deviné demande plusieurs essais. La boucle correcte
repasse à chaque fois par le firmware d'origine, et ne touche jamais `app0` :

1. `/revert` sur le firmware custom (s'il tourne encore et que le WiFi
   répond) — retour au firmware d'origine dans `app0` :
   ```bash
   curl.exe -X POST http://<ip-de-la-k-touch>/revert
   ```
   Si le WiFi ne répondait pas du tout, le sauvetage automatique (minuteur ou
   compteur de démarrages, voir plus haut) a déjà fait ce même retour tout
   seul — cette étape est alors inutile.
2. Corriger le code, recompiler (`idf.py build`).
3. `/update` **du firmware d'origine**, pas du nôtre — c'est lui qui expose
   cette route, et c'est lui qui tourne à ce stade puisqu'on vient d'y
   revenir. Il écrit la nouvelle version dans le slot inactif (`app1`) et
   démarre dessus.
4. Essai, observation. Si le nouveau code casse quelque chose avant que le
   WiFi ne se connecte, le sauvetage automatique ramène l'appareil au
   firmware d'origine dans `app0` de lui-même, sans jamais y avoir touché.
5. Retour à l'étape 1.

Une fois la bonne adresse IP du firmware custom retrouvée (elle peut changer
d'un redémarrage à l'autre selon le bail DHCP — `/status` sur le firmware
d'origine ou les journaux réseau permettent de la retrouver), consulter
`/log` pour lire les logs de démarrage et confirmer ce qui a changé.

## Retrouver une voie série, si le besoin s'en fait sentir

La K-Touch expose son UART via un pont sur le port USB-C. Un câble USB-C ne
transportant que l'alimentation (ce qui est le cas dans ce montage) n'énumère
aucun port : c'est pourquoi ce document entier existe. Pour retrouver un accès
série, il faut un câble USB-C qui transporte effectivement les lignes de
données (pas uniquement les lignes d'alimentation), branché sur un hôte qui
peut énumérer le périphérique série qui en résulte. Une fois un port série
disponible, les outils habituels (`esptool`, `idf.py monitor`, sauvegarde et
restauration octet par octet décrites dans `docs/hardware/partitions.md`)
redeviennent utilisables.
