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
| Mise à jour (`/update`) | requête HTTP volontaire | WiFi fonctionnel |
| Retour manuel (`/revert`) | requête HTTP volontaire | WiFi fonctionnel |
| Sauvetage automatique | absence de connexion WiFi à l'échéance | rien — ni écran, ni tactile, ni réseau |

Le firmware n'écrit **jamais** dans `app0`. Toute écriture (`/update`, comme le
sauvetage automatique) cible `esp_ota_get_next_update_partition(NULL)`, qui
désigne toujours le slot inactif — `app1` tant que le firmware custom tourne
depuis `app0`, et inversement. Il n'y a pas de calcul d'offset à la main nulle
part dans ce code.

## Routes HTTP exposées

Le serveur écoute sur le port 80, à l'adresse IP journalisée au démarrage
(`adresse IP : ...` dans les logs, visible aussi via `/status`).

| Route | Méthode | Rôle |
|---|---|---|
| `/` | GET | page d'état minimale, avec liens vers les autres routes |
| `/status` | GET | JSON : slot en cours, version, temps depuis le démarrage, mémoire libre, tactile disponible ou non |
| `/log` | GET | texte brut, contenu du journal réseau en RAM (dernières lignes de log) |
| `/revert` | POST | bascule vers l'autre slot OTA et redémarre |
| `/update` | POST | reçoit une image applicative brute (`.bin`), l'écrit dans le slot inactif et redémarre dessus |

`/revert` et `/update` sont en **POST** délibérément : en GET, n'importe quelle
requête d'un navigateur, d'un aspirateur de liens ou d'un scanner réseau
redémarrerait l'appareil.

## Installer le firmware d'origine par WiFi (sans câble)

Utile si l'appareil tourne déjà sur le firmware custom et qu'on veut revenir
au stock BIGTREETECH sans passer par `/revert` (par exemple pour réinstaller
une version différente de celle actuellement dans `app0`) :

```bash
curl -X POST --data-binary @K-Touch_v1.1.0_app.bin http://<ip-de-la-k-touch>/update
```

L'image doit être une image applicative ESP-IDF brute (pas l'image de flash
complète 16 Mio) — le type d'image qu'`esptool` écrirait normalement à
l'offset `0x10000` ou `0x490000`. L'appareil écrit l'image dans le slot
inactif, bascule le boot dessus, puis redémarre.

## Revenir au firmware d'origine (le WiFi marche, l'affichage est raté)

C'est le cas couvert par `/revert` : le firmware custom a démarré, le WiFi a
joint le réseau (donc le sauvetage automatique s'est désarmé), mais l'écran ou
le tactile ne fonctionnent pas comme attendu. Comme le firmware d'origine
n'est jamais écrasé dans son slot, revenir au stock ne demande aucun
téléversement :

```bash
curl -X POST http://<ip-de-la-k-touch>/revert
```

L'appareil bascule immédiatement sur l'autre slot et redémarre dessus.

## Sauvetage automatique (le WiFi ne répond pas du tout)

C'est le seul mécanisme qui fonctionne quand tout va mal : il ne dépend ni du
réseau, ni de l'écran, ni du tactile. Au démarrage, avant même de tenter quoi
que ce soit d'autre, un compte à rebours de `CONFIG_KTOUCH_RESCUE_TIMEOUT_MS`
(90 secondes par défaut) est armé. Si le WiFi n'est pas connecté à son
échéance — mauvais SSID, mot de passe incorrect, réseau hors de portée — le
firmware bascule seul la partition de démarrage sur l'autre slot et
redémarre. Aucune intervention n'est nécessaire : il suffit d'attendre.

Le seul endroit du firmware qui désarme ce minuteur est le gestionnaire de
`IP_EVENT_STA_GOT_IP` dans `wifi.c`, c'est-à-dire une connexion WiFi
effectivement réussie (adresse IP obtenue). Rien d'autre ne le désarme.

## Itérer sur le pinout (le cas d'usage principal de ce jalon)

Corriger un pinout mal deviné demande plusieurs essais. Le cycle, sans jamais
débrancher l'appareil :

1. Corriger le code, recompiler (`idf.py build`).
2. Envoyer le nouveau binaire :
   ```bash
   curl -X POST --data-binary @firmware/build/ktouch-custom.bin http://<ip-de-la-k-touch>/update
   ```
3. L'appareil redémarre sur le nouveau binaire, réarme le sauvetage, retente
   le WiFi. Si le nouveau code casse quelque chose avant que le WiFi ne se
   connecte, le sauvetage automatique ramène l'appareil au slot précédent
   (celui d'où l'on vient de faire `/update`, puisque c'est justement le
   slot qui devient inactif) 90 secondes plus tard — sans jamais toucher à
   `app0`.
4. Une fois la bonne adresse IP retrouvée (elle peut changer d'un
   redémarrage à l'autre selon le bail DHCP), consulter `/log` pour lire les
   logs de démarrage et confirmer ce qui a changé.

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
