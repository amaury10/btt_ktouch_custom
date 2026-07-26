# BTT K-Touch Custom — document de conception

**Date :** 2026-07-26
**Dépôt :** https://github.com/amaury10/btt_ktouch_custom
**Statut :** conception validée, jalon 1 à planifier

---

## 1. Contexte

La BIGTREETECH K-Touch est un écran tactile autonome de 5 pouces destiné au
pilotage d'imprimantes Klipper via Moonraker. BTT en a arrêté le développement :
le dernier firmware publié est le `v1.1.0` du 11 novembre 2024, et le dépôt
officiel n'a plus bougé depuis le 15 janvier 2025. Le binaire de cette version
« finale » s'identifie lui-même comme `K-Touch_V1_1_0_Beta1.bin` — BTT a laissé
une beta comme dernière release, avec 43 tickets ouverts et aucune pull request.

Le matériel, lui, est excellent et sous-exploité : un ESP32-S3 avec 8 Mo de PSRAM,
16 Mo de flash, un écran IPS 800×480, du tactile capacitif, du Wi-Fi, de l'USB
host et une batterie.

## 2. Objectifs

Le projet poursuit deux buts qui partagent la même fondation technique.

Le premier est communautaire : redonner un firmware vivant, ouvert et compilable
à un appareil abandonné par son fabricant, pour que ses possesseurs ne dépendent
plus d'une beta figée.

Le second est personnel : détourner la K-Touch pour en faire la console de
pilotage d'un projet de tracker astrophotographique existant, qui expose déjà un
serveur sur Raspberry Pi.

Ces deux usages convergent, et c'est ce qui rend le projet cohérent plutôt que
dispersé : dans les deux cas l'appareil est un client HTTP/WebSocket sur le
réseau local, affichant un état distant et renvoyant des commandes. Seul l'écran
applicatif diffère.

## 3. Matériel cible — faits vérifiés

Les éléments ci-dessous ont été établis par analyse directe des binaires stock
téléchargés depuis le dépôt officiel, sauf mention contraire.

| Élément | Valeur | Source |
|---|---|---|
| SoC | ESP32-S3 (Xtensa LX7 double cœur, `chip_id` 0x9 dans l'en-tête d'image) | en-tête `K-Touch_v1.1.0_firmware.bin` |
| PSRAM / Flash | 8 Mo octal / 16 Mo | wiki BTT + champ `flash_size` de l'image |
| Écran | IPS 5", 800×480, RGB565, bus parallèle 16 bits, mode DE | wiki BTT + BSP Panda Touch |
| Tactile | GT911 capacitif sur I²C | wiki BTT + BSP Panda Touch |
| Firmware stock | `K-Touch_V1_1_0_Beta1.bin`, ESP-IDF v5.1.1-dirty, build du 11/11/2024 | descripteur `esp_app_desc_t` |
| Autres | USB-C (CH340K, flash et debug), USB host, I²C d'extension, batterie | wiki BTT |

La table de partitions a été lue directement dans `K-Touch_v1.1.0_partition.bin`
et se révèle **identique** à celle du Panda Touch :

| Partition | Type | Sous-type | Offset | Taille |
|---|---|---|---|---|
| `nvs` | data | nvs | `0x9000` | 20 Kio |
| `otadata` | data | ota | `0xe000` | 8 Kio |
| `app0` | app | ota_0 | `0x10000` | 4608 Kio |
| `app1` | app | ota_1 | `0x490000` | 4608 Kio |
| `spiffs` | data | spiffs | `0x910000` | 7040 Kio |
| `coredump` | data | coredump | `0xff0000` | 64 Kio |

**Deux slots OTA de 4,5 Mo chacun.** C'est le fait le plus structurant du projet :
il permet d'installer un firmware expérimental dans `app1` sans jamais effacer le
firmware stock qui reste dans `app0`, et de revenir en arrière en basculant
`otadata`.

## 4. État de l'art et contraintes de licence

Trois ressources externes existent, avec des statuts juridiques très différents
qu'il faut traiter explicitement puisque le dépôt a vocation à devenir public.

`bigtreetech/PandaTouch_IDF` est un composant ESP-IDF publié par BTT. Il contient
le pilote LCD RGB, le pilote tactile GT911, le rétroéclairage PWM piloté en LEDC,
un wrapper USB MSC et une documentation de pinout. C'est techniquement la base
idéale, et elle vient du fabricant lui-même.

**Mais son statut juridique est défaillant, et il faut le traiter comme tel.**
Son README arbore un badge « License: MIT » qui pointe vers un fichier `LICENSE`
**qui n'existe pas** : le dépôt amont n'en contient aucun, et l'API de licence de
GitHub répond 404. Le fichier `NOTICE` de Prusa-Connect-Touch le déclare « BTT,
MIT », mais cette affirmation repose sur le même badge décoratif. Or du code
publié sans licence reste, par défaut, sous droit d'auteur plein : les conditions
d'utilisation de GitHub autorisent à le consulter et à le forker sur la
plateforme, pas à le redistribuer ni à le sous-licencier.

Conséquence pratique sur l'architecture du dépôt : **ce composant n'est pas
recopié dans le dépôt**. Il est référencé comme sous-module Git, donc chacun le
récupère depuis la source officielle et nous ne redistribuons rien. Cela lève
entièrement la question, et donne au passage une gestion de version propre.

En parallèle, une issue sera ouverte chez BTT pour leur demander d'ajouter le
fichier `LICENSE` que leur README annonce déjà — c'est utile à toute la
communauté, pas seulement à ce projet. Si BTT ne répond pas et que la
dépendance devient gênante, le repli est une réécriture propre du BSP : les
numéros de broches et les timings sont des faits, non protégeables, et le reste
n'est qu'une fine couche de collage au-dessus de `esp_lcd_rgb_panel` et
`esp_lcd_touch_gt911`, tous deux publiés par Espressif sous Apache-2.0.

`nomadsgalaxy/Prusa-Connect-Touch` est un firmware ESP-IDF + LVGL qui tourne déjà
sur K-Touch. Il est techniquement très pertinent mais publié sous **OCL v1.1+
SWAtt v1**, une licence qui impose à tout dérivé de conserver une attribution
« built on Prusa-Connect-Touch » visible à la fois dans l'interface utilisateur
et dans le code source. Cette obligation se propagerait jusqu'à l'application
astro. **Décision : ce dépôt ne reprend aucune ligne de code de ce projet.** Il
peut être cité comme prior art et consulté comme documentation, mais le code
reste indépendant.

`Disttrack/PandaTouch_streamDeck` est sous licence MIT et montre une approche
PlatformIO sur le même matériel, mais cible le Panda Touch uniquement.

**Licence retenue pour ce dépôt : MIT.** Elle ne s'applique qu'à notre propre
code ; le sous-module BTT conserve son statut amont, quel qu'il devienne.

Point à trancher avant publication : les binaires stock de BTT sont distribués
sans licence explicite. Ils ne seront donc **pas redistribués** dans le dépôt ;
la documentation pointera vers le dépôt officiel et fournira les scripts pour les
télécharger et les analyser localement.

## 5. Ce que le reverse engineering doit encore produire

L'analyse statique déjà menée a réglé certaines questions et en a ouvert d'autres.

La surface de l'API Moonraker utilisée par le firmware stock a été entièrement
extraite des chaînes du binaire : `/printer/info`, `/printer/objects/list`,
`/printer/objects/query`, `/printer/print/{start,pause,resume,cancel}`,
`/printer/gcode/script`, `/printer/emergency_stop`, `/printer/query_endstops/status`,
`/server/files/{list,upload,metadata,gcodes}`, `/server/gcode_store`, `/server/info`.
Cela documente gratuitement le protocole côté imprimante et servira de cahier des
charges pour l'application Klipper.

En revanche, **le pinout exact du panneau de la K-Touch reste inconnu**. Le seul
pinout publié est celui du Panda Touch 7", et le projet Prusa-Connect-Touch note
lui-même dans son README que la K-Touch 5" « may differ on a few panel GPIOs or
timings ». J'ai balayé le binaire stock à la recherche du tableau des 16 GPIO de
données et des structures de timings : **rien n'est extractible statiquement**,
parce qu'ESP-IDF construit `esp_lcd_rgb_panel_config_t` sur la pile à l'exécution
plutôt que de la stocker en `.rodata`. Seuls un désassemblage Xtensa ou une
vérification sur matériel peuvent trancher.

Le conteneur `product.img` (partition `spiffs`, 2,75 Mo) n'est **ni SPIFFS, ni
LittleFS, ni FAT**, malgré le sous-type déclaré. Son en-tête (`00 00 2b 00 ea 26
07 00 05 …`) suit le même schéma que celui du Panda Touch et il ne contient aucun
nom de fichier en clair. C'est un format maison non documenté — cible de RE à
part entière, dont dépendrait toute capacité à re-thématiser l'interface stock.

**Question résolue : l'appareil porte bien le firmware K-Touch d'origine.** Un
doute existait, parce que le pack de recovery présent localement contient du
firmware **Panda Touch** (projet interne `knomi_p1`, API `api.bambulab.com`) et
que son journal atteste d'une écriture complète réussie sur un ESP32 le
19 novembre 2024. L'appareil, joignable sur le réseau local, sert une page de
configuration dont le titre visible est « BTT K TOUCH SETTINGS MANAGER ». Or
cette chaîne n'existe que dans le binaire K-Touch ; le firmware Panda Touch ne
contient qu'une variante « PANDA-TOUCH ».

Piège à connaître au passage : le binaire K-Touch contient **les deux** chaînes,
et sa balise `<title>` annonce « BTT PANDA-TOUCH SETTINGS MANAGER » — vestige du
tronc commun que BTT n'a pas renommé. Se fier au titre de l'onglet du navigateur
mène donc à la conclusion inverse de la bonne ; c'est le `<h1>` qui distingue les
deux appareils.

Conséquence pratique pour le jalon 1 : cette page expose un point d'entrée
`update`, c'est-à-dire le mécanisme OTA du fabricant. Il constitue une voie
d'installation alternative à la manipulation manuelle d'`otadata`, et
potentiellement plus sûre puisque c'est le firmware d'origine qui gère alors la
bascule de slot. À évaluer une fois la sauvegarde faite, sans remplacer le dump
qui reste le préalable absolu.

## 6. Architecture

Le dépôt s'organise en quatre couches, de la plus matérielle à la plus
applicative, plus un espace dédié au reverse engineering.

```
docs/hardware/     pinout K-Touch vérifié, partitions, batterie, procédures
re/                scripts d'analyse des binaires, notes de désassemblage
bsp/               support matériel K-Touch, au-dessus du composant BTT (sous-module)
core/              socle commun : Wi-Fi, OTA deux slots, log réseau, UI LVGL de base
apps/klipper/      client Moonraker            (objectif communautaire)
apps/astro/        client du serveur rpi        (tracker astrophoto)
```

`bsp/` isole tout ce qui touche aux GPIO, aux timings du panneau, au tactile et à
l'alimentation. C'est la seule couche qui connaît le matériel ; tout le reste
s'en abstrait. Si le pinout de la K-Touch diffère de celui du Panda Touch, la
correction est locale à cette couche.

`core/` fournit ce dont les deux applications ont besoin sans distinction :
connexion Wi-Fi et provisionnement, mise à jour OTA vers le slot inactif, sortie
de log sur le réseau (indispensable puisque l'appareil fonctionne sur batterie,
sans câble), et les primitives d'interface LVGL communes.

`apps/` contient les deux applications. Elles ne partagent pas de code entre
elles ; elles partagent `core/` et `bsp/`.

La frontière importante est celle entre `core/` et `apps/` : une application doit
pouvoir être écrite sans rien savoir des GPIO ni de la mécanique OTA, et le socle
doit pouvoir évoluer sans casser les applications.

## 7. Jalon 1 — preuve de vie

Le premier jalon valide la chaîne complète et répond empiriquement à la question
du pinout, avant tout investissement dans le désassemblage.

**Contenu.** Sauvegarder l'intégralité des 16 Mo de flash de l'appareil. Utiliser
la chaîne **ESP-IDF v5.5.5**, déjà installée et vérifiée. Le BSP déclare un
plancher à 5.1 et a été validé sous 5.3.1, mais sa compatibilité avec la 5.5.5 a
été contrôlée dans les sources : le pilote RGB y est présent, simplement déplacé,
et tous les champs de configuration que le BSP renseigne existent encore. Le seul
écart est un champ déprécié qui produira un avertissement de compilation attendu.
La v6.0 est écartée pour ce jalon — version majeure à changements incompatibles,
postérieure au dernier commit du BSP. Ce choix est indépendant de l'IDF v5.1.1 du
firmware stock, que nous ne recompilons pas. Construire ensuite un firmware
minimal s'appuyant sur le BSP, en partant du pinout Panda Touch. L'installer dans le
slot `app1` et le rendre actif. Constater que l'écran s'allume et que le tactile
répond.

**Pourquoi cet ordre.** Si le pinout Panda Touch fonctionne tel quel sur la
K-Touch, la question ouverte est réglée en une heure et sans désassemblage. S'il
ne fonctionne pas, on saura exactement quoi chercher dans Ghidra — la fonction
d'initialisation LCD — au lieu d'explorer un binaire de 2,2 Mo à l'aveugle.

**Filet de sécurité.** La table de partitions stock est conservée telle quelle,
sans aucune modification — c'est la condition de la réversibilité, et c'est aussi
ce qui garde le firmware maison compatible avec le mécanisme OTA de BTT. Le
firmware stock n'est jamais écrasé : il reste dans `app0` pendant que
l'expérimentation vit dans `app1`, et un basculement de `otadata` suffit à
revenir. En dernier recours, le flash complet est
reprogrammable en USB-C via le CH340K, et le dump initial permet une restauration
octet par octet. Aucune étape du jalon 1 n'est irréversible.

**Critères de succès.** Le dump fait 16 777 216 octets et son contenu à
l'offset `0x8000` correspond à la table de partitions attendue. Le firmware
maison démarre depuis `app1`. Le rétroéclairage s'allume et une image de test
s'affiche sans artefact ni scintillement. Un appui sur l'écran produit des
coordonnées cohérentes dans le log. Le retour au firmware stock est effectué au
moins une fois pour prouver la réversibilité.

## 8. Non-objectifs

Pour rester concentré, le projet ne cherche pas à reproduire l'interface stock à
l'identique, ne vise pas le Panda Touch comme cible de premier ordre (même si le
BSP partagé devrait le permettre presque gratuitement), et ne redistribue pas les
binaires de BTT. Le décodage du conteneur `product.img` n'est pas nécessaire au
firmware maison, qui embarque ses propres ressources — il reste un objectif de
documentation, pas un prérequis.

## 9. Résultat du jalon 1 — atteint le 26 juillet 2026

**Les cinq critères de réussite sont validés**, malgré une contrainte apparue en
cours de route qui a fait tomber la méthode initiale.

Le pinout du Panda Touch 7 pouces fonctionne tel quel sur la K-Touch 5 pouces,
affichage **et** tactile, sans aucune adaptation. Détail et mesures dans
`docs/hardware/pinout.md`. C'est l'apport publiable du jalon : personne ne
l'avait établi, et la réserve « may differ on a few panel GPIOs or timings » du
README de Prusa-Connect-Touch est levée.

Le firmware maison a démarré depuis `app1`, tourné 49 minutes sans un seul
redémarrage — compteur resté à 1, tas libre inchangé à l'octet près — et
l'appareil est revenu au firmware d'origine sur commande `/revert`. Le firmware
d'origine n'a jamais été écrasé, et ses identifiants WiFi sont intacts.

**Ce qui a changé par rapport à la conception initiale.** Le port USB-C de
l'appareil s'est révélé inexploitable : `esptool` hors jeu, donc **aucune
sauvegarde des 16 Mo**. Tout le filet de sécurité de la section 7 tombait, et la
tâche d'installation devenait dangereuse — le firmware de preuve de vie n'ayant
pas de pile réseau, l'installer aurait été un aller sans retour. Le filet a donc
été déplacé dans le firmware lui-même : sauvetage automatique, compteur de
démarrages en mémoire RTC, et retour par `/revert`. Les deux premiers ont été
éprouvés en conditions réelles avant que le troisième ne serve.

**Quatre défauts trouvés en relecture auraient rendu l'appareil définitivement
injoignable**, et méritent d'être consignés parce qu'aucun n'était visible en
lisant le code isolément : une écriture de la configuration WiFi dans la NVS
**partagée entre les deux slots**, qui aurait effacé les identifiants du firmware
d'origine ; un `ESP_ERROR_CHECK` sur l'initialisation de l'écran, qui
transformait l'hypothèse même du jalon en boucle de redémarrage plus rapide que
le minuteur de sauvetage ; un test mémoire PSRAM actif par défaut, qui appelle
`abort()` depuis `cpu_start.c` avant qu'aucun secours n'existe ; et une route de
mise à jour qui, depuis `app1`, ne pouvait écrire que sur `app0`.

**Deux pièges d'outillage, à connaître.** `sdkconfig.defaults` ne s'applique
qu'aux symboles absents du `sdkconfig` déjà généré — un réglage ajouté après la
première compilation n'a aucun effet, silencieusement. Et la police Montserrat
compilée par défaut dans LVGL ne couvre que l'ASCII de base : tout caractère
au-delà s'affiche en carré vide, ce qui compte quand l'écran est le seul canal de
diagnostic.

## 10. Suite

Les jalons suivants restent à cadrer. Le socle est désormais acquis : matériel
documenté, firmware compilable, et un chemin d'installation et de retour éprouvé
qui ne demande qu'un réseau. L'ordre entre l'application Klipper et l'application
astro reste à décider.

Deux chantiers de rétro-ingénierie restent ouverts et indépendants : le format du
conteneur `product.img`, toujours non documenté, et le désassemblage du firmware
d'origine si l'on veut comprendre son protocole plutôt que le remplacer.
