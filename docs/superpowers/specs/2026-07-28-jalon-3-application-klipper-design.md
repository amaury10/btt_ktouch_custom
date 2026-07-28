# Jalon 3 — L'application Klipper en profondeur — document de conception

Validé en brainstorming le 2026-07-28. Référence fonctionnelle : KlipperScreen
(https://klipperscreen.github.io/KlipperScreen/). KlipperScreen est sous
AGPL-3.0 : il sert de **modèle fonctionnel et ergonomique uniquement** — aucune
ligne de son code (Python/GTK) n'est reprise ni transposée. Chaque fonction
qu'il expose passe par une API Moonraker documentée : c'est la carte des
endpoints à consommer, rien de plus.

## 1. Ce qu'on construit, et pourquoi

Le jalon 2b a livré le squelette : un écran d'accueil qui *montre* une
impression et trois actions. Le jalon 3 livre l'application que la communauté
attend d'un écran local : piloter la machine au repos (jog, homing,
températures, macros, lancement d'impression) et régler une impression en
cours (consignes par chauffeur, babystep Z, vitesse, flux).

**La doléance n°1 remontée sur le K-Touch d'origine est l'impossibilité de
lancer des macros.** Ce jalon en fait sa première livraison visible : la
preuve de bout en bout de la tranche socle (3a) est précisément « lister les
macros de la machine et en lancer une ». Ce que le firmware d'origine n'a
jamais su faire est la première chose que le nôtre montre.

Machines de référence réelles : une Creality CR-10 S5 modifiée (grand format,
mono-extrudeur) et une Snapmaker U1 (toolchanger multi-têtes) — les deux
extrêmes que le dimensionnement doit servir. Cible communautaire : jusqu'à la
Prusa XL (5 têtes) et les toolchangers à 8 outils.

## 2. Décisions structurantes (actées au brainstorming)

- **Transport : WebSocket Moonraker**, comme KlipperScreen. Connexion
  persistante (`espressif/esp_websocket_client`, composant géré officiel du
  registre — comme LVGL, il s'ajoute via `idf_component.yml`), JSON-RPC 2.0,
  abonnement `printer.objects/subscribe` — l'état arrive **poussé**
  (`notify_status_update`), les commandes partent en RPC
  (`printer.gcode.script`, etc.). **La boucle HTTP à 1 Hz du jalon 2a reste
  intégralement en place comme repli** : WS en panne ⇒ retour au sondage, sans
  autre dégradation que la latence.
- **État dimensionné à 8 extrudeurs** + plateau + 4 ventilateurs, en tableaux
  de taille fixe avec compteurs de présence. Le contrat fondateur du socle
  (POD sans pointeurs, détection de changement par memcmp, double tampon)
  survit intact.
- **Navigateur de fichiers avec miniatures** : liste `server.files`, tri,
  pagination, et les vignettes PNG que les slicers embarquent dans le gcode,
  décodées en PSRAM (8 Mo disponibles).
- **Panneaux machine embarqués dans ce jalon** : extrusion/filament, vis de
  nivellement (screws tilt), limites machine. **Bed mesh : visualisation 2D
  seulement** (grille en aplats de couleur) et **uniquement si le coût de
  rendu reste raisonnable** — sinon écartée sans regret. **Jamais de 3D**
  (gadget, décision utilisateur).
- **Tout se construit et se vérifie sur simulateur** (décision utilisateur :
  l'appareil est éteint, l'implémentation n'attend pas la validation
  matérielle du 2b). Risque assumé et documenté : si le matériel révèle un
  problème de fond (perfs LVGL, tactile), une partie de l'interface sera à
  retoucher.

## 3. Découpage en tranches

Chaque tranche a son propre plan, ses tâches, ses revues — même processus que
le jalon 2b. Ordre : 3a → 3b → 3c → 3d → 3e → 3f (3b/3c échangeables, 3e picorable, 3f déplaçable plus tôt si le besoin de bascule se fait sentir).

### 3a — Transport WebSocket + état riche + preuve macro

Le socle invisible, terminé par une preuve visible :

1. **Client WS** : `esp_websocket_client` vers `ws://<hôte>/websocket`,
   identification (`server.connection.identify`), abonnement aux objets
   nécessaires, réception `notify_status_update`, réémission des abonnements à
   la reconnexion (backoff exponentiel borné). Tout le parsing JSON-RPC est
   extrait en **fonctions pures compilables sur PC** (`moonraker_rpc.c`),
   testées dans le harnais comme `moonraker_parse.c` l'est déjà.
2. **`etat_klipper_t` v2** (voir §5).
3. **Le contrat backend ne change pas** (voir §4) — c'est ce qui protège le
   fork astro et le critère « zéro ligne dans core/ ».
4. **Preuve de bout en bout** : un écran minimal liste les macros remontées
   dans l'état, un tap envoie `printer.gcode.script {script: "<macro>"}`, le
   résultat (succès ou erreur Klipper) revient en notification. Vérifiable sur
   simulateur (backend factice « U1 » multi-têtes avec macros synthétiques),
   puis sur les deux machines réelles dès qu'elles sont joignables.

### 3b — Accueil Idle

Quand rien n'imprime, l'écran d'accueil devient un poste de pilotage :

- état complet : températures de tous les chauffeurs présents, position,
  axes référencés, outil actif ;
- **jog** : pad XY + colonne Z, pas sélectionnables 0.1/1/10/100 mm, envoi
  `G91`/`G1` via script gcode, désactivé si l'axe n'est pas référencé ;
- **homing** : par axe et global (`G28`), avec confirmation si un axe
  référencé va bouger ;
- **températures manuelles** : tap sur une tuile ⇒ clavier numérique existant
  (jalon 2b) ⇒ consigne par chauffeur, préréglages PLA/PETG/ABS/off ;
- **macros** : la liste complète (les `_préfixées` sont filtrées, comme
  KlipperScreen), un tap lance ; **macros à paramètres** en fin de tranche
  (invite construite depuis les `params` par défaut, clavier existant) ;
- **bouton Imprimer** ⇒ navigateur de fichiers (3d ; tant que 3d n'est pas
  livré, le bouton est absent — jamais un bouton mort).

### 3c — Impression enrichie

L'écran d'impression du 2b gagne le pilotage en cours :

- tuiles de température **cliquables** (consigne par chauffeur, les 8
  extrudeurs si présents, outil actif marqué) ;
- **babystep Z** : ±0.01 / ±0.05 mm (`SET_GCODE_OFFSET Z_ADJUST=... MOVE=1`),
  valeur cumulée affichée ;
- **vitesse** (M220) et **flux** (M221) en pourcentage, molette ou ±10 % ;
- **macros accessibles pendant l'impression** (M600, purge, LED…) — même
  panneau que 3b, ouvert depuis l'écran d'impression.

### 3d — Fichiers + miniatures

- liste `server.files/list` (racine gcodes), tri nom/date/taille, dossiers,
  pagination (listes potentiellement longues — chargement paresseux par page,
  le widget arrive avec cet écran conformément au principe d'extraction) ;
- **miniatures** : Moonraker sert les vignettes PNG embarquées par les
  slicers ; décodage PNG en PSRAM, cache borné avec éviction, placeholder si
  absente ou trop grande. **Position de repli (décision utilisateur), même
  convention que la bed mesh** : si les miniatures s'avèrent trop coûteuses
  ou fragiles (décodage, mémoire, latence), l'écran est livré en liste
  seule — nom, date, taille — pleinement fonctionnel, et l'abandon des
  vignettes est consigné avec sa raison. La liste n'attend jamais les
  miniatures : elle s'affiche d'abord, les vignettes arrivent ensuite,
  chacune quand elle est décodée ;
- métadonnées avant lancement (temps estimé, filament) ;
- lancement avec confirmation (`printer.print.start`), et c'est CE chemin qui
  active le bouton Imprimer de 3b ;
- **clé USB comme seconde source** (ajout utilisateur — le port USB host du
  K-Touch monte une clé sur le stock, la parité est attendue) : le composant
  `usb_host_msc` est déjà dans les dépendances du build. Modèle retenu : la
  clé apparaît comme un onglet/une racine du même navigateur ; imprimer un
  fichier de la clé = **le téléverser vers Moonraker** (`server.files`
  upload, avec progression) puis lancer — jamais un chemin d'impression
  parallèle, Klipper ne lit que ce que Moonraker héberge. Réserve
  d'honnêteté : c'est la seule partie du jalon **intestable sur simulateur**
  (pas d'USB simulé) — la logique pure (FAT, liste, découpage d'upload) se
  teste sur PC, l'intégration ne se valide que sur l'appareil ; elle est
  livrée en dernier dans la tranche et consignée « différée matériel » tant
  que la K-Touch n'a pas tourné.

### 3e — Panneaux machine

- **extrusion/filament** : sélection d'outil (T0…T7 si présents),
  charger/décharger/purger (longueur et vitesse paramétrées), température
  minimale d'extrusion respectée (refus + notification sinon) ;
- **vis de nivellement** : assistant `SCREWS_TILT_CALCULATE`, résultats par
  vis (sens et tours), relance ;
- **limites machine** : vitesse max, accélération, square corner velocity
  (`SET_VELOCITY_LIMIT`), lecture des valeurs courantes ;
- **bed mesh 2D** : grille `lv_canvas` en aplats colorés min/max —
  livrée seulement si le rendu reste simple ; sinon la tranche se termine
  sans elle et le document le consigne.

### 3f — Gestion de parc (multi-imprimantes)

Ajout utilisateur en cours de spec : pouvoir se connecter à plusieurs
imprimantes. Modèle retenu — celui de KlipperScreen : **une imprimante active
à la fois, bascule rapide**, jamais N connexions simultanées (N liaisons et N
états sur un 5 pouces coûtent cher et ne servent guère).

- **Profils nommés** en NVS (espace `ktouch`, jusqu'à 8 : nom affichable +
  hôte:port), l'écran de configuration du 2b évolue en gestionnaire de parc
  (ajouter, renommer, supprimer, choisir l'actif) ;
- **bascule** depuis la barre d'état (le nom de l'imprimante active y est
  affiché — tap ⇒ liste du parc) : arrêt propre de la liaison courante
  (`arreter`), démarrage sur le nouveau profil, l'état repart de zéro (grisé
  « connecting » — jamais l'état de l'ancienne machine présenté comme celui
  de la nouvelle) ;
- le profil actif survit au redémarrage ; la migration depuis la clé unique
  du 2a est automatique (elle devient le profil 1).

## 4. Architecture : comment le push entre dans un socle fait pour le pull

Le contrat `backend_desc_t` (demarrer/rafraichir/arreter/commande) **ne change
pas**. Le WS ne remplace pas la boucle : il la nourrit.

- Le client WS tourne dans sa propre tâche (celle d'`esp_websocket_client`) et
  ne touche **jamais** ni l'état partagé ni LVGL : il dépose les
  `notify_status_update` analysés dans une **boîte aux lettres** bornée
  (dernier état complet fusionné, pas une file d'événements — un état écrase
  le précédent, on ne rejoue pas l'historique).
- `backend_moonraker.rafraichir()` — toujours appelé par la boucle du socle,
  toujours à sa cadence — **draine la boîte aux lettres** quand le WS est en
  ligne, et retombe sur le GET HTTP existant sinon. La détection de
  changement, le double tampon, la liaison, le grisage : rien ne bouge.
- La cadence de la boucle passe de 1 Hz fixe à **adaptative** : 250 ms quand
  le WS est en ligne (le drain est quasi gratuit), 1 s en repli HTTP. C'est le
  seul changement demandé à `core/boucle.c`, derrière une valeur fournie par
  le backend.
- Les **commandes** partent en RPC via le WS quand il est en ligne (réponse
  corrélée par id JSON-RPC ⇒ le résultat réel de Klipper revient, pas juste
  « HTTP 200 »), en POST HTTP sinon. Le seam d'échec asynchrone du 2b sert
  tel quel.

Ce montage garde le critère du fork intact : un backend « astro » reste un
`backend_desc_t` ; le WS est un détail interne du backend Moonraker.

## 5. `etat_klipper_t` v2

Toujours un POD à taille fixe, sans pointeur. Ajouts :

- `extrudeurs[8]` : { actuelle, consigne, presente } ; `nb_extrudeurs` ;
  `outil_actif` ;
- `plateau` inchangé ; `ventilateurs[4]` : { vitesse, present } ;
- `position[3]`, `axes_references` (masque XYZ), `deplacement_absolu` ;
- `vitesse_pct`, `flux_pct`, `babystep_z` (µm signés) ;
- `macros[48]` × `MACRO_NOM_MAX 32` + `nb_macros` (au-delà de 48, tronqué +
  drapeau `macros_tronquees` — l'UI l'affiche honnêtement) ;
- l'existant du 2b (impression, fichier, temps restant…) inchangé.

Taille estimée ~2,5 Ko : memcmp et double tampon restent négligeables. La
liste des macros ne change qu'à un redémarrage Klipper : elle est remplie à la
connexion (`printer.objects/list` + configfile) et sur `notify_klippy_ready`,
pas à chaque cycle.

Actions de commande ajoutées (chaînes du registre existant, arguments via
`arguments_json` déjà prévu au contrat) : `jog`, `home`, `consigne_temp`,
`macro`, `babystep`, `vitesse`, `flux`, `outil`, `imprimer_fichier`,
`charger_filament`, `decharger_filament`, `purger`, `limites`.

## 6. Interface : ce qui est réutilisé, ce qui naît

Réutilisé tel quel : navigation, habillage (barre d'état, notifications,
grisage §5.3), clavier (texte + numérique), confirmation (y compris libellé
de refus personnalisé), tuiles, progression, formateurs.

Nouveaux widgets, chacun livré avec le premier écran qui l'exige (principe
d'extraction inchangé) : pad de jog, sélecteur de pas, molette/±  pour les
pourcentages, liste paginée à chargement paresseux, vignette d'image (PNG
PSRAM), grille de mesh (si retenue).

**Trois paliers de mise en page pour les outils** (décision utilisateur) : le
choix se fait sur `nb_extrudeurs`, pour qu'un mono-extrudeur ne soit jamais
cantonné à une petite cellule quand il a huit fois la place :

- **1 tête** : les grandes tuiles du 2b, inchangées — valeur en Montserrat 48,
  consigne dessous, la place sert à la lisibilité ;
- **2 à 4 têtes** : grille moyenne (2×2 max), valeur en 28, l'outil actif
  marqué ; c'est le palier de la Snapmaker U1 ;
- **5 à 8 têtes** : grille compacte (2×4), valeur en 20, tap ⇒ vue détaillée
  du chauffeur (consigne, préréglages) puisque la cellule n'a plus la place
  d'un réglage direct ; c'est le palier Prusa XL et au-delà.

Le palier s'applique partout où les outils s'affichent (accueil Idle, écran
d'impression, panneau filament), choisi par un même helper partagé — jamais
recalculé à la main écran par écran. Le plateau garde sa tuile propre à tous
les paliers. Les scénarios du backend factice couvrent les trois paliers
(CR-10 = 1, U1 = 4, synthétique = 8) et une capture par palier fait partie
des livrables de revue.

Les écrans restent des `ecran_desc_t` ; l'accueil du 2b évolue en « accueil
impression » et un « accueil idle » distinct apparaît — le socle choisit
lequel afficher selon `etat_impression`, par la même mécanique de navigation
qu'aujourd'hui. Les seules modifications attendues dans `core/` et `ui/` sont
explicitement listées : la cadence adaptative de la boucle (§4), le schéma
NVS des profils de parc dans `reglages.c` (§3f, avec migration automatique
depuis la clé unique du 2a), et l'affichage du nom de l'imprimante active
dans l'habillage (§3f). Tout le reste passe par les contrats existants — et
le critère 8 (le jouet du 2b compile inchangé) le vérifie mécaniquement.

## 7. Sécurité et honnêteté (héritées, non renégociables)

- Invariants du jalon 1 : sauvetage armé, NVS partagée jamais effacée, aucune
  défaillance locale fatale, `/revert` toujours joignable.
- §5.3 : la barre d'état seule affiche la liaison ; données périmées grisées ;
  boutons désactivés **visiblement** (styles résolus, acquis 2b).
- Jamais de mise à jour optimiste : une commande envoyée n'anticipe rien à
  l'écran, l'état poussé/sondé est la seule vérité.
- Le jog et le homing sont **désactivés** hors ligne et pendant une
  impression (sauf babystep) ; l'extrusion respecte la température minimale.
- E-STOP : comportement du 2b conservé ; la question « rester actif en
  liaison dégradée ? » reste parquée pour arbitrage utilisateur.

## 8. Tests

Même modèle à trois niveaux que le 2b :

1. **PC pur** : `moonraker_rpc.c` (JSON-RPC, notify_status_update, fusion
   dans l'état v2) exhaustivement testé dans le harnais ; la boîte aux
   lettres testée via `boucle_cycle` (contrat de drain).
2. **Simulateur** : le backend factice gagne des scénarios « CR-10 »
   (mono-extrudeur) et « U1 » (4 têtes + macros synthétiques, dont une à
   paramètres et une qui échoue) ; captures PNG à chaque écran, ouvertes et
   regardées.
3. **Klipper réel simulé** (proposé par l'utilisateur) : l'image Docker
   communautaire `mainsail-crew/virtual-klipper-printer` — un vrai Klipper
   (MCU simulé) et un vrai Moonraker — tourne dans WSL2/Docker sur le PC de
   dev. Le client WS de la tranche 3a se valide contre le protocole
   authentique (abonnements, notify_status_update, erreurs Klipper réelles,
   vraies macros), pas contre une simulation de la documentation. C'est aussi
   ce qui comble en partie la « comparaison à un vrai Moonraker » différée
   depuis le jalon 2a. Le simulateur K-Touch y accède par une option
   `--hote <adresse:port>` qui branche le vrai backend Moonraker (HTTP puis
   WS) à la place du factice — le même code que la cible, moins le matériel.
4. **Machines réelles** : CR-10 S5 et Snapmaker U1 dès qu'elles sont
   joignables — la validation finale, sur les deux extrêmes du dimensionnement.

## 9. Non-objectifs du jalon 3

Caméra, timelapse, tableaux de bord multi-imprimantes simultanés (le parc de
3f bascule, il n'agrège pas — la synthèse légère du parc est un candidat
jalon 4, voir §11), éditeur de configuration Klipper (candidat jalon 4),
courbes de température (graphes temporels — le tuning PID du jalon 4 lèvera
ce non-objectif), rendu 3D de quoi que ce soit, moteur de mise à jour (reste
le jalon 2c), fork astro (jalon dédié).

## 10. Critères de succès

1. Sur simulateur, scénario U1 : les macros s'affichent, un tap en lance une,
   le résultat revient en notification. (La doléance n°1 est levée.)
2. Le WS se connecte, s'abonne, pousse l'état ; on coupe le WS ⇒ repli HTTP
   sans autre dégradation que la latence ; il revient ⇒ ré-abonnement seul.
3. Accueil Idle : jog, homing, consignes manuelles et macros fonctionnent sur
   simulateur ; jog refusé si axe non référencé.
4. Impression : consignes par chauffeur, babystep, vitesse, flux opèrent, et
   l'affichage ne fait que refléter l'état poussé.
5. Fichiers : liste paginée avec miniatures, lancement avec confirmation.
6. Panneaux : filament, vis, limites opérationnels ; mesh 2D livrée ou
   explicitement écartée avec sa raison.
7. Parc : deux profils enregistrés, la bascule change de machine proprement
   (jamais l'état de l'une présenté comme celui de l'autre), le profil actif
   survit au redémarrage.
8. Le backend jouet du 2b compile et tourne toujours sans modification —
   le contrat n'a pas bougé.
9. Validation sur les deux machines réelles (différable si injoignables,
   alors consignée comme différée, jamais comme faite).

## 11. Suite

Après le jalon 3 : le moteur de mise à jour (2c, jamais oublié), puis le fork
astro — dont ce jalon aura prouvé le socle par les neuf critères ci-dessus.

**Candidats consignés pour un jalon 4** (demandes utilisateur du 2026-07-28,
à re-brainstormer le moment venu, sous réserve explicite « si ça rentre dans
l'écran » — un 800×480 tactile, pas un poste de travail) :

- **éditeur de configuration « basique »** : lecture et édition des fichiers
  de config Klipper via `server.files` (printer.cfg et includes), avec
  sauvegarde + redémarrage Klipper — « basique » : édition texte au clavier
  tactile, pas d'assistance sémantique ;
- **vue tuning PID** : graphe température/consigne en direct pendant un
  `PID_CALIBRATE`, lancement de l'autotune, application du résultat — c'est
  la première vraie courbe temporelle du projet (le non-objectif « graphes »
  du jalon 3 saute à ce moment-là) ;
- **écran de synthèse du parc** : l'exception assumée au modèle « une active
  à la fois » de 3f — une vue légère qui interroge périodiquement chaque
  profil et affiche a minima l'état de chacune (idle, impression à X %,
  erreur, éteinte/injoignable). Pas des tableaux de bord complets : un état
  par ligne, et un tap bascule vers cette imprimante.
