# Backend et écran jouets — mode d'emploi du fork

Ce répertoire est la preuve, et le modèle, que le socle (`firmware/main/core/`
et `firmware/main/ui/`) héberge une application qui n'est pas Klipper sans en
modifier une seule ligne. Le fork astro part d'ici.

## Les quatre fichiers à écrire

| Fichier | Contrat rempli | Contenu |
|---|---|---|
| `backend_jouet.h` / `.c` | `backend_desc_t` (`core/backend.h`) | Un état à deux champs (`compteur`, `libelle`) ; `rafraichir()` incrémente `compteur` ; `commande()` gère l'action `"reset"`. Aucune I/O. |
| `ecran_jouet.h` / `.c` | `ecran_desc_t` (`ui/ecran.h`) | Un titre, la valeur du compteur, un bouton Reset qui appelle `ui_commander("reset", NULL)`. |

Points de contrat qui comptent réellement, vérifiés en les cassant pendant
l'écriture de ce backend (voir `host-test/tests/test_jouet.c` pour les tests
qui les épinglent) :

- **Le tampon `etat` reçu par `rafraichir()` est remis à zéro par le socle
  avant CHAQUE appel** (`core/backend.h`, commentaire de `rafraichir`). Un
  compteur qui doit survivre d'un cycle à l'autre se porte dans une variable
  `static` de fichier (voir `g_compteur` dans `backend_jouet.c`), jamais en
  le relisant depuis `etat`. C'est exactement le bug qui a coûté le plus cher
  au jalon précédent (voir `backend_factice.c`, commentaire CRITICAL 1).
- **`demarrer()` ne doit rien écrire dans `etat` au-delà de zéro** si vous
  voulez que `generation` reste à 0 juste après le démarrage (le tampon
  "avant" est lui aussi à zéro à ce moment-là ; y écrire quoi que ce soit
  fait déjà avancer `generation` avant le premier `rafraichir()` réel — RED
  observé en écrivant ce backend, voir `backend_jouet.c`).
- **`commande()` rend `ESP_ERR_NOT_SUPPORTED` sur une action inconnue**,
  jamais un succès silencieux — l'interface doit pouvoir griser un bouton en
  le sachant (`core/backend.h`).
- **`mettre_a_jour()` grise sur `donnees_perimees=true`, systématiquement à
  chaque appel** (jamais de façon incrémentale) et ne pose jamais sa propre
  boîte d'erreur réseau (spécification §5.3, seule l'habillage affiche l'état
  de la liaison).
- **Un échec synchrone de `ui_commander()`** (file pleine, boucle pas
  démarrée) se notifie via `habillage_notifier(..., true)` — jamais une boîte
  d'erreur posée par l'écran. Copier le motif de
  `firmware/main/apps/klipper/ecrans/ecran_accueil.c::executer_commande()`.

## Les deux lignes d'enregistrement

Au point d'assemblage de l'application (côté simulateur : `simulateur/main.c` ;
côté appareil : votre propre `app_main.c`, jamais `core/` ni `ui/`) :

```c
navigation_empiler(&ECRAN_JOUET);
/* ... */
source_etat_sim_demarrer(backend_jouet_desc());   /* ou boucle_demarrer() sur cible */
```

C'est tout. Rien d'autre à toucher dans `core/` ou `ui/` pour que l'écran
s'affiche, se mette à jour, et que son bouton envoie une commande.

## Le seul vrai accroc : `ui/habillage.c`

`habillage_pomper()` (appelé à chaque image/cycle pour rafraîchir la barre
d'état ET relayer l'état applicatif à l'écran visible) porte un tampon
`etat_klipper_t` **concret** — pas générique — pour lire l'état via
`ui_etat_instantane()`. Ce couplage est **documenté et assumé** en tête de
`ui/habillage.h` : *"Un fork avec une autre application (une machine
non-Klipper) adapte ce fichier-là aux deux sites, pas ecran.h/navigation.c
qui restent réutilisables tels quels."* Le second site est
`libelle_commande()`, qui traduit les actions `BACKEND_ACTION_*` de Klipper
en mots anglais pour le bandeau de notification.

Deux façons de composer avec ça, selon votre situation :

1. **Fork réel (recommandé) : adaptez `ui/habillage.c`.** Remplacez le
   tampon `etat_klipper_t g_etat;` par le type d'état de votre application
   (ou par un `uint8_t[]` de la bonne taille si votre app change de backend
   à l'exécution), et adaptez `libelle_commande()` à vos propres actions.
   C'est le chemin que le commentaire de tête de `habillage.h` anticipe
   explicitement — vous n'inventez rien, vous suivez le contrat écrit.

2. **Contrainte "zéro ligne dans `core/`/`ui/`" (le cas de la tâche 11
   elle-même, pas celui d'un fork ordinaire) : contournez `habillage_pomper()`
   pour le relais d'état applicatif, en gardant l'appel pour la barre de
   statut.** `ui_etat_instantane()` (façade générique, `void*`/taille),
   `habillage_donnees_perimees()` et `navigation_mettre_a_jour()` sont tous
   trois des fonctions PUBLIQUES et déjà génériques de `ui/` — ce sont
   exactement les trois appels que `habillage_pomper()` fait déjà en interne.
   Les rappeler vous-même depuis votre point d'assemblage, avec un tampon de
   la taille de votre propre état, referme la boucle sans modifier `ui/` :

   ```c
   static void jouet_pomper(void)
   {
       etat_jouet_t etat;
       uint32_t generation;
       liaison_etat_t liaison;
       if (ui_etat_instantane(&etat, sizeof(etat), &generation, &liaison)) {
           navigation_mettre_a_jour(&etat, habillage_donnees_perimees(liaison));
       }
   }
   ```

   Voir `simulateur/main.c` (`jouet_pomper()`) pour l'implémentation réelle,
   appelée à côté de `habillage_pomper()` à chaque cycle. Le prix de cette
   option : la pastille de connexion de la barre d'état (son texte et sa
   couleur) reste figée sur "connecting"/gris pour votre application, parce
   que l'appel *interne* de `habillage_pomper()` à `ui_etat_instantane()`
   échoue sa propre vérification de taille (tampon `etat_klipper_t`, jamais
   le vôtre) et ne remplit donc jamais sa variable locale de liaison. Le
   reste de la barre (titre, heure, wifi, batterie) reste correct : rien
   d'autre n'y dépend de la taille de l'état applicatif. Voir
   `task-11-report.md` pour le détail complet et les captures qui le
   montrent.

Un fork ordinaire n'a aucune raison de vivre avec ce défaut : l'option 1 le
corrige en trois lignes. L'option 2 n'existe que parce que la tâche 11
mesure littéralement `git diff --stat -- firmware/main/core firmware/main/ui`
et doit rester vide.
