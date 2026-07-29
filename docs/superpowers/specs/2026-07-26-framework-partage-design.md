# Framework partagé — document de conception (jalon 2)

**Date :** 2026-07-26
**Dépôt :** https://github.com/amaury10/btt_ktouch_custom
**Statut :** conception validée, plan d'implémentation à écrire
**Prérequis :** jalon 1 atteint — voir `2026-07-26-btt-ktouch-custom-design.md`

---

## 1. Ce qu'on construit, et pourquoi maintenant

Le jalon 1 a livré un firmware qui démarre, affiche, réagit au tactile et sait
revenir en arrière sans câble. Il ne fait rien d'utile : c'est une preuve de vie.

Deux applications doivent maintenant se construire dessus. Un client
**Klipper/Moonraker**, public et destiné à être repris par la communauté. Et une
console de pilotage pour un **tracker astrophotographique**, qui vivra dans un
fork.

En comparant leurs deux interfaces, on constate qu'elles sont **le même programme
avec un vocabulaire différent**. Le tracker expose un `GET /api/status` agrégé
puis des `POST` de commande (`/api/mount/track`, `/stop`, `/goto`, `/jog`, plus
caméra, chauffages, moteurs, GPS, services). Moonraker, dont la surface a été
extraite du firmware d'origine au jalon 1, suit exactement la même forme :
`GET /printer/objects/query` agrégé, puis `POST` de commande. Dans les deux cas :
interroger un état, l'afficher, envoyer des ordres ponctuels.

Le moment est le bon précisément parce qu'**aucune des deux applications n'existe
encore**. Extraire un socle après coup, de deux bases divergentes, coûte
beaucoup plus cher que de poser la frontière avant.

## 2. Décisions structurantes

Quatre décisions ont été prises avant la conception détaillée, et tout le reste
en découle.

**L'ambition est le pilotage complet, pas un tableau de bord.** Côté astro,
piloter tout ce que le serveur expose, hors traitement d'images qui reste au PC.
Côté Klipper, viser la qualité de l'interface locale existante. Ce n'est donc pas
un jalon mais une série : ce document ne couvre que le socle.

**L'astro vivra dans un fork**, le dépôt public ne portant que Klipper. Cette
décision a une conséquence qu'il faut énoncer clairement : **la qualité de la
séparation devient plus critique, pas moins.** Avec deux applications dans le
même dépôt, une abstraction bancale ne compile plus et se corrige aussitôt. Avec
un fork, elle se paye en douleur de fusion, silencieusement, à chaque évolution
du socle. D'où le critère de réussite le plus important de ce jalon :

> **Le fork astro ne doit jamais avoir besoin de modifier `core/`.** S'il doit y
> toucher, c'est qu'un point d'extension manquait — et c'est un défaut à corriger
> dans le socle, pas à contourner dans le fork.

**Le socle s'extrait, il ne se spécule pas.** Construire une couche générique
sans consommateur réel est la façon la plus fiable de construire la mauvaise.
Chaque brique entre dans le socle le jour où un écran réel en a besoin.

**La frontière est un registre de backends et d'écrans**, décrit en section 4.

## 3. Architecture

Quatre couches, de la plus matérielle à la plus applicative.

```
apps/klipper/      backend Moonraker + ses écrans        ← le fork remplace cette couche
core/              boucle, transport, état, navigation,
                   habillage, widgets, réglages, mise à jour
                   (+ backend factice, toujours présent)
firmware/main/     amorçage : WiFi, journal, sauvetage    ← acquis au jalon 1
components/        BSP PandaTouch_IDF (sous-module)       ← code BTT, jamais modifié
```

La ligne de partage tient en une phrase : **le socle sait *comment* parler à une
machine et *comment* présenter ; l'application sait *ce que* la machine
signifie.** Qu'est-ce qu'une température de buse, qu'est-ce qu'une ascension
droite, quels écrans existent, que fait ce bouton — cela seul appartient à
l'application.

Le socle possède la boucle principale. Il démarre le WiFi, le journal et le
sauvetage, instancie le backend configuré, lance l'interrogation périodique,
construit la barre d'état, puis affiche l'écran d'accueil déclaré par
l'application. **Une application n'a pas de `main`.**

## 4. Les deux points d'extension

### 4.1 Le backend

Il répond à une seule question : que se passe-t-il sur la machine, et comment lui
donner un ordre.

```c
typedef struct {
    const char *nom;                 /* "moonraker", "astro", "factice" */
    size_t      taille_etat;         /* le socle alloue ; le backend n'alloue pas */

    esp_err_t (*demarrer)(void *etat, const backend_hote_t *hote);
    esp_err_t (*rafraichir)(void *etat);   /* remplit l'état ; appelé par la boucle */
    void      (*arreter)(void *etat);

    esp_err_t (*commande)(void *etat, const char *action, const char *arguments_json);

    /* Ajout jalon 3a (optionnel) : cadence d'interrogation souhaitée en ms.
     * NULL ⇒ la boucle garde son défaut. Un backend peut l'accélérer quand un
     * transport poussé (WebSocket) rend le drainage quasi gratuit. Champ
     * additif, à sémantique NULL = comportement d'origine : n'invalide aucun
     * backend antérieur (le jouet du 2b compile inchangé). */
    uint32_t  (*periode_ms)(void *etat);
} backend_desc_t;
```

`rafraichir` n'impose pas *comment*. Le backend Moonraker pourra tenir un
WebSocket et se contenter de recopier ce qu'il a reçu ; le backend astro
interrogera `/api/status` en HTTP. Le socle ne connaît pas la différence : il
sait seulement qu'après l'appel, l'état est à jour ou une erreur est remontée.

**Le socle alloue l'état, jamais le backend.** Ce n'est pas un détail de style.
Sur un appareil sans port série, une fuite dans le chemin de rafraîchissement se
manifeste par un redémarrage au bout de plusieurs heures, exactement quand
personne n'est devant. Centraliser l'allocation retire cette classe de bugs des
contributions. C'est aussi ce qui rend possible la détection de changement
décrite en 5.1, qui exige une structure de taille connue et de disposition
stable.

### 4.2 L'écran

Il ne sait ni interroger, ni parler réseau. Il lit un état et émet des actions.

```c
typedef struct {
    const char *id;                  /* "accueil", "temperatures", "fichiers" */
    const char *titre;               /* affiché dans la barre d'état */

    void (*construire)(lv_obj_t *parent, void *contexte);
    void (*mettre_a_jour)(const void *etat, bool donnees_perimees, void *contexte);
    void (*detruire)(void *contexte);
} ecran_desc_t;
```

*(`donnees_perimees` ajouté en cours de jalon — revue de la tâche 4 : sans lui,
un écran n'a aucun chemin structurel pour recevoir la péremption et griser, et
rien n'empêche d'afficher des zéros comme des mesures.)*

C'est cette séparation qui rend une contribution locale : ajouter un panneau de
macros, c'est un fichier, un descripteur, une ligne d'enregistrement. Et c'est
elle qui permet au fork astro de fournir un `backend_desc_t` et sa dizaine
d'écrans sans jamais ouvrir `core/`.

Un écran devient aussi testable : il reçoit un état, il produit des widgets. Pas
de réseau, pas de fil d'exécution, pas d'horloge.

## 5. Flux de données

### 5.1 Double tampon et détection de changement

La boucle réseau ne remplit jamais l'état que les écrans lisent. Le socle garde
deux tampons : il **met à zéro** le tampon arrière, appelle `rafraichir`, compare
au tampon avant, et ne permute qu'en cas de différence. Un écran lit donc
toujours un instantané cohérent, jamais une structure à moitié réécrite, et ne se
redessine que quand quelque chose a bougé.

La mise à zéro préalable est nécessaire : sans elle, le remplissage laissé par
l'alignement de la structure ferait échouer la comparaison au hasard, et l'écran
se redessinerait sans raison.

### 5.2 Cloisonnement des fils d'exécution

LVGL n'est pas réentrant, et c'est la première source de bugs sur ce genre de
projet. Deux règles absolues, portées par le socle :

- **La tâche réseau ne touche jamais un widget.** Elle remplit l'état, puis
  planifie la mise à jour sur le fil LVGL via le mécanisme du BSP.
- **Un écran n'émet jamais de requête HTTP depuis un rappel de bouton.** Un
  `POST` peut prendre plusieurs secondes et gèlerait l'interface. Les commandes
  sont mises en file vers la tâche réseau ; le résultat revient en notification.

Écrire ces contraintes une fois dans le socle évite à chaque contributeur de les
redécouvrir à ses frais.

### 5.3 L'état de la connexion appartient à l'habillage

La liaison à l'hôte a quatre états : **connexion en cours**, **en ligne**,
**dégradée** (une interrogation a échoué, pas encore de quoi renoncer) et **hors
ligne**. La barre d'état est **seule** à les afficher.

> Un écran ne montre **jamais** de boîte d'erreur réseau. Quand les données sont
> périmées, il les grise ; l'habillage explique pourquoi.

Sans cette règle, chaque panneau finit par inventer sa propre façon de dire « je
n'ai pas de nouvelles ». Même principe pour les échecs de commande : le socle
affiche la notification, l'écran ne s'en occupe pas.

### 5.4 Navigation et cycle de vie

La navigation est une pile. Empiler construit, dépiler détruit — **pas de mise en
cache**. Avec l'ambition de parité visée, garder dix écrans construits en mémoire
finit par saturer, et un redémarrage nocturne est précisément ce qu'on ne sait
pas déboguer. Seul l'écran visible reçoit `mettre_a_jour`. Le retour et l'accueil
vivent dans l'habillage, donc identiques partout.

## 6. Ce que le socle fournit

**Transport** : client HTTP, boucle d'interrogation, délais d'attente, réessais
avec temporisation croissante, détection de données périmées, machine à états de
connexion.

**Habillage** : barre d'état unique — qualité WiFi, santé de la liaison, heure,
batterie —, retour, accueil.

**Widgets communs, et uniquement ceux que la tranche verticale exige** : clavier
tactile (texte et numérique) présenté en dialogue modal rendant une valeur ;
dialogue de confirmation pour les actions destructrices ; surface de
notification ; tuile de valeur ; barre de progression.

Les autres widgets pressentis — éditeur de consigne numérique, graphe temporel,
liste à chargement paresseux — ne sont **pas** écrits dans ce jalon. Ils
arriveront avec le premier écran qui en aura besoin, faute de quoi ils seraient
des hypothèses non vérifiées : c'est l'application directe du principe d'extraction
de la section 2, et la tentation de les écrire « pendant qu'on y est » est
précisément ce contre quoi ce principe existe.

Le clavier est le plus gros morceau et il est indiscutablement partagé : adresse
de l'hôte, commande gcode, recherche de fichier, mot de passe WiFi. LVGL fournit
`lv_keyboard`, mais l'habiller en dialogue modal qui rend une valeur est un vrai
travail, à ne pas refaire deux fois.

**Réglages persistants** : adresse et port de l'hôte, backend choisi, luminosité,
extinction d'écran. En NVS, **dans un espace de noms dédié au projet**, et
**sans jamais effacer la partition** — voir section 9.

## 7. Moteur de mise à jour

Le jalon 1 avait **supprimé** toute route de mise à jour, et il faut expliquer
pourquoi cette décision s'inverse ici plutôt que de la contredire en silence.

Notre firmware tourne depuis `app1`, donc le seul autre slot est `app0`, celui du
firmware d'origine. `esp_ota_begin` en taille inconnue efface la partition cible
**avant** de recevoir le moindre octet : une mise à jour aurait donc détruit le
stock, et avec lui la cible du sauvetage. Le motif n'était pas « l'OTA est
dangereux » mais « nous n'avons nulle part où écrire ».

C'est la situation du développement, pas celle de l'usage. Un possesseur de
K-Touch qui adopte ce firmware au quotidien ne repassera pas par le stock à
chaque version. **Le moteur appartient donc au socle** : tout le monde en a
besoin, aucune application ne le fournira.

Il **améliore** la sûreté une fois le pas franchi. Aujourd'hui, un retour arrière
mène au firmware d'origine : sûr, mais il faut tout refaire. Quand les deux slots
portent notre firmware, un retour arrière mène à **la version précédente de notre
propre firmware**, qui a toujours le WiFi, le journal et `/revert`. On passe d'un
filet qui annule tout à un A/B classique.

**Garde-fous, non négociables.** Le moteur lit le descripteur applicatif du slot
cible avant d'écrire — la logique de `ktouch-cli.py image`, embarquée. S'il y
reconnaît le firmware d'origine, il **refuse par défaut**. Pas un avertissement
noyé dans une page : un refus, levable seulement par une confirmation explicite
énonçant ce qu'on perd.

Le franchissement reste réversible : BTT publie ses images, la partition `spiffs`
n'est jamais touchée, donc le moteur sait reflasher le firmware d'origine plus
tard.

Le retour arrière natif d'ESP-IDF reste inutilisable — le bootloader exécuté est
celui de BTT et nous ne le remplaçons pas. Le minuteur de sauvetage et le
compteur de démarrages du jalon 1 le remplacent, et s'appliquent à toute nouvelle
version installée.

## 8. Tests

**Le simulateur est la pièce la plus rentable du jalon**, et pas d'abord pour la
vitesse de développement. Construire l'interface sur PC — LVGL sur SDL, la même
bibliothèque, les mêmes écrans — **rend une contribution possible sans posséder
l'appareil**. Pour un projet qui vise la reprise communautaire, c'est
probablement le facteur décisif : la K-Touch est abandonnée, personne n'en
achètera de nouvelles, et le vivier de contributeurs est essentiellement composé
de gens qui n'en ont pas.

**Trois niveaux**, du plus rapide au plus lent :

1. **Analyseurs JSON sur PC**, via la cible `linux` d'ESP-IDF. Ce sont des
   fonctions pures — des octets entrent, une structure sort — et c'est là que
   vivront la plupart des bugs, parce que c'est là qu'on interprète le JSON
   parfois surprenant de Moonraker.
2. **Écrans dans le simulateur**, à l'œil.
3. **Sur l'appareil**, une route `/state` ajoutée au serveur de diagnostic du
   jalon 1, qui vide l'état courant en JSON. Elle permet de vérifier à distance
   que le backend interprète correctement une vraie machine.

**Le backend factice fait partie du socle**, et c'est plus qu'une commodité. Il
produit un état synthétique et permet d'exercer les cas pénibles : température à
zéro, impression à 99 %, machine déconnectée, valeurs aberrantes. Il fait tourner
le simulateur sans aucune machine.

Surtout, il règle un risque du modèle en fork : sans lui, le dépôt public
n'aurait qu'**un seul** consommateur réel de l'abstraction, et une abstraction
cassée n'apparaîtrait qu'à la prochaine fusion du fork. Avec lui, il y en a deux
en permanence.

## 9. Invariants hérités du jalon 1

Ils ne sont pas renégociables par une application ni par une contribution.

- Le **sauvetage automatique** et le **compteur de démarrages** en mémoire RTC
  restent armés, quel que soit le contenu applicatif.
- Les réglages vivent dans un **espace de noms NVS dédié**. La partition `nvs`
  est **partagée avec le firmware d'origine** : ne jamais l'effacer, jamais y
  écrire de configuration WiFi (`WIFI_STORAGE_RAM` reste posé après
  `esp_wifi_init`).
- **Aucune écriture en partition applicative** hors du moteur de mise à jour de
  la section 7, avec ses garde-fous.
- **Aucune défaillance locale n'est fatale** : ni écran, ni tactile, ni serveur.
  On journalise et on continue, pour qu'une panne reste diagnosticable à
  distance.

## 10. Tranche verticale du jalon

L'enchaînement le plus court qui traverse tout le socle :

1. un écran de **première configuration** — adresse de l'hôte, type de machine ;
2. l'**écran d'accueil Klipper** — températures buse et plateau avec consignes,
   progression, nom du fichier, temps restant ;
3. trois actions : **pause/reprise**, **annulation**, **arrêt d'urgence**, les
   deux dernières passant par le dialogue de confirmation.

C'est court et ça traverse absolument tout : transport, interrogation, double
tampon, détection de changement, barre d'état et ses quatre états, navigation
avec au moins un empilement, commandes en file avec remontée d'échec, clavier
tactile — obligatoire dès la première configuration — et réglages persistants.

Aucune brique du socle ne sera écrite sans qu'un usage réel ne la justifie le
jour même.

## 11. Non-objectifs

Explicitement hors de ce jalon, et chacun sera ajouté par l'écran qui en aura
besoin : navigateur de fichiers, console gcode, macros, graphes temporels,
calibrations, gestion de plusieurs machines, et le fork astro lui-même.

## 12. Critères de succès

1. Le firmware démarre, demande sa configuration au premier lancement, et affiche
   l'état d'une vraie imprimante Klipper.
2. Les trois actions fonctionnent, et un échec de commande produit une
   notification visible sans quitter l'écran.
3. Débrancher l'hôte fait passer la barre d'état par **dégradée** puis **hors
   ligne**, les données affichées sont grisées, et aucune boîte d'erreur
   n'apparaît.
4. Le simulateur affiche les mêmes écrans sur PC, avec le backend factice, sans
   matériel.
5. Les analyseurs JSON passent leurs tests sur la cible `linux`.
6. Le moteur de mise à jour refuse d'écraser le firmware d'origine, et l'accepte
   après confirmation explicite.
7. **Test décisif du modèle en fork** : écrire un backend jouet supplémentaire —
   quelques dizaines de lignes, un écran — sans modifier une seule ligne de
   `core/`. Si c'est impossible, le socle n'est pas fini.

## 13. Suite

Le jalon 3 sera l'application Klipper proprement dite, en profondeur. Le fork
astro suivra, et son premier contact avec le socle sera la meilleure mesure de la
qualité de ce jalon.
