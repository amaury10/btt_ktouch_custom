*Cette page est également disponible en [anglais](licence-du-composant-btt.en.md).*

# La licence manquante de `PandaTouch_IDF`

Ce document consigne le constat de licence qui a déterminé l'architecture du
dépôt — architecture depuis abandonnée sans décision, voir plus bas — et
reproduit le signalement **posté chez BIGTREETECH**
([`PandaTouch_IDF#1`](https://github.com/bigtreetech/PandaTouch_IDF/issues/1)).

## Le constat

Le composant [`bigtreetech/PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF),
dont provient tout le support matériel utilisé ici, affiche dans son README un
badge « License: MIT » pointant vers un fichier `LICENSE` **qui n'existe pas**.
L'API de licence de GitHub ne détecte aucune licence sur ce dépôt.

Du code publié sans licence reste sous droit d'auteur plein : les conditions
d'utilisation de GitHub permettent de le consulter et de le forker sur la
plateforme, pas de le redistribuer ni de l'inclure dans une œuvre dérivée.

## La conséquence, appliquée puis abandonnée sans décision

À l'origine, le composant était référencé en **sous-module Git** et n'était
jamais recopié ici : rien de son code n'était redistribué. C'était la seule
raison de ce choix d'architecture, autrement inutilement contraignant.

**Ce n'est plus le cas, et ça n'a jamais été arbitré.** Le commit `64c8d55`
(2026-07-31, « affichage tear-free stable + integration du BSP ») a versé le
composant dans l'arbre : 28 fichiers suivis par git, dont 1 826 lignes de
source sous `firmware/components/PandaTouch_IDF/`. Deux modifications locales
s'y sont ajoutées depuis — `b9fda5f` (reprise du montage VFS sur unit-attention
SCSI, +28 lignes dans `pandatouch_msc.c`) et `32f8c9a` (+5 lignes dans
`pandatouch_board.h`) — ce qui en fait un fork, pas une copie. `.gitmodules` ne
déclare plus que `simulateur/lvgl`.

Le dépôt redistribue donc aujourd'hui du code sans licence, ce que cette
architecture existait pour éviter. Tant qu'il reste privé, rien n'est
distribué ; **la question doit être tranchée avant toute publication.** Le
volume à reprendre n'est pas uniforme : l'affichage et le tactile ne pèsent que
511 lignes de collage au-dessus de `esp_lcd_rgb_panel` et
`esp_lcd_touch_gt911` (Apache-2.0, Espressif), tandis que `pandatouch_msc.c`
en fait 801 à lui seul, soit 44 % du composant.

À noter que d'autres projets vendorisent le composant en se fiant au badge — le
fichier `NOTICE` de `nomadsgalaxy/Prusa-Connect-Touch` le liste comme
« BigTreeTech, MIT ». Cette affirmation repose sur la même image décorative.

## Repli si la situation devait gêner

Réécrire proprement le support matériel : les numéros de broches et les timings
sont des faits, non protégeables — et `docs/hardware/pinout.md` les documente
désormais indépendamment, vérifiés sur matériel. Le reste n'est qu'une couche de
collage au-dessus de `esp_lcd_rgb_panel` et `esp_lcd_touch_gt911`, tous deux
publiés par Espressif sous Apache-2.0.

## Signalement envoyé

Le texte ci-dessous a été **posté chez BIGTREETECH** :
[`bigtreetech/PandaTouch_IDF#1`](https://github.com/bigtreetech/PandaTouch_IDF/issues/1).
Il demande l'ajout d'un fichier `LICENSE` et propose une PR avec le texte MIT
standard. Il avait été rédigé bien plus tôt puis mis de côté ; il a été
**corrigé juste avant l'envoi** — il affirmait encore que ce dépôt ne
redistribuait rien du composant, ce qui n'est plus vrai (voir la section
précédente), et portait un lien resté à l'état d'espace réservé.

**Tant qu'il n'y a pas de réponse, rien ne change** : le dépôt redistribue du
code sans licence, et la question reste à trancher avant publication.

---

**Titre :** `README shows an MIT badge but the repository contains no LICENSE file`

**Corps :**

```markdown
Hi, and thanks for publishing this component — the LCD, GT911 and USB MSC glue
saved us a lot of bring-up work.

There is a licensing gap that makes the component hard to build on, and I think
it is unintentional:

- `README.md` displays a `License: MIT` badge, and the badge links to `LICENSE`.
- There is no `LICENSE` file in the repository.
- GitHub's license API returns 404 for this repository, so no license is
  detected in the repository metadata either.
- The README text itself reads "provided under the MIT License (assumed)".

Under default copyright law, code published without a license is "all rights
reserved": GitHub's Terms of Service let people view and fork it on the
platform, but grant no right to redistribute it or to ship it inside a
derivative work. So despite the badge, downstream projects cannot safely vendor
this component — and several already do, on the strength of the badge alone
(for instance `nomadsgalaxy/Prusa-Connect-Touch`, whose NOTICE file lists this
component as "BigTreeTech, MIT").

Would you consider adding the MIT license text as a `LICENSE` file at the
repository root, with the appropriate copyright line? That single file would
make the badge accurate, and let the component be used the way the badge
already suggests it can be.

Happy to open a PR with the standard MIT text if that is easier — you would
just need to confirm the copyright holder and year.

For context on what the component enabled: we used it to bring up a BIGTREETECH
K-Touch (the 5-inch model), and were able to confirm that the Panda Touch
7-inch pinout and timings work on it unchanged, display and GT911 touch alike.
That did not seem to be documented publicly anywhere. Happy to share the
details if they are useful to you or to others.
```

---
