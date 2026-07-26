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
| `/log` | GET | texte brut, contenu du journal réseau en RAM (dernières lignes de log) |
| `/revert` | POST | bascule vers l'autre slot OTA et redémarre |

`/revert` est en **POST** délibérément : en GET, n'importe quelle requête d'un
navigateur, d'un aspirateur de liens ou d'un scanner réseau redémarrerait
l'appareil.

## Revenir au firmware d'origine (le WiFi marche, l'affichage est raté)

C'est le cas couvert par `/revert` : le firmware custom a démarré, le WiFi a
joint le réseau (donc le sauvetage automatique s'est désarmé), mais l'écran ou
le tactile ne fonctionnent pas comme attendu. Comme le firmware d'origine
n'est jamais écrasé dans son slot, revenir au stock ne demande aucun
téléversement :

```bash
curl -X POST http://<ip-de-la-k-touch>/revert
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

**Le compteur de démarrages** couvre tout ce que le minuteur ne voit pas : une
panne plus rapide que 90 secondes — panique, chien de garde, débordement de
pile, échec d'initialisation de la PSRAM. Il vit en mémoire RTC
(`RTC_NOINIT_ATTR`), donc survit à un redémarrage logiciel comme à une
panique, mais pas à une coupure d'alimentation. Incrémenté tout au début
d'`app_main`, avant même le minuteur, il bascule immédiatement l'appareil sur
l'autre slot dès qu'il dépasse trois démarrages consécutifs sans qu'une
adresse IP n'ait jamais été obtenue. Une connexion WiFi réussie le remet à
zéro, au même endroit que le minuteur.

Dans les deux cas, aucune intervention n'est nécessaire : il suffit d'attendre
que l'appareil se rétablisse de lui-même.

## Itérer sur le pinout (le cas d'usage principal de ce jalon)

Corriger un pinout mal deviné demande plusieurs essais. La boucle correcte
repasse à chaque fois par le firmware d'origine, et ne touche jamais `app0` :

1. `/revert` sur le firmware custom (s'il tourne encore et que le WiFi
   répond) — retour au firmware d'origine dans `app0` :
   ```bash
   curl -X POST http://<ip-de-la-k-touch>/revert
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
