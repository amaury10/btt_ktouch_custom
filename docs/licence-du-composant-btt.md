# La licence manquante de `PandaTouch_IDF`

Ce document consigne le constat de licence qui a déterminé l'architecture du
dépôt, et conserve un texte de signalement rédigé mais **non envoyé**.

## Le constat

Le composant [`bigtreetech/PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF),
dont provient tout le support matériel utilisé ici, affiche dans son README un
badge « License: MIT » pointant vers un fichier `LICENSE` **qui n'existe pas**.
L'API de licence de GitHub ne détecte aucune licence sur ce dépôt.

Du code publié sans licence reste sous droit d'auteur plein : les conditions
d'utilisation de GitHub permettent de le consulter et de le forker sur la
plateforme, pas de le redistribuer ni de l'inclure dans une œuvre dérivée.

## La conséquence, déjà appliquée

Le composant est référencé en **sous-module Git** et n'est jamais recopié dans ce
dépôt. Rien de son code n'est redistribué ici. C'est la seule raison de ce choix
d'architecture, qui serait autrement inutilement contraignant.

À noter que d'autres projets vendorisent le composant en se fiant au badge — le
fichier `NOTICE` de `nomadsgalaxy/Prusa-Connect-Touch` le liste comme
« BigTreeTech, MIT ». Cette affirmation repose sur la même image décorative.

## Repli si la situation devait gêner

Réécrire proprement le support matériel : les numéros de broches et les timings
sont des faits, non protégeables — et `docs/hardware/pinout.md` les documente
désormais indépendamment, vérifiés sur matériel. Le reste n'est qu'une couche de
collage au-dessus de `esp_lcd_rgb_panel` et `esp_lcd_touch_gt911`, tous deux
publiés par Espressif sous Apache-2.0.

## Signalement rédigé, non envoyé

Le texte ci-dessous a été préparé pour une issue chez BIGTREETECH, puis mis de
côté. Il est conservé au cas où la question redeviendrait d'actualité — par
exemple si l'on souhaitait un jour intégrer le composant plutôt que le
référencer. **Rien n'a été publié chez un tiers.**

---

**Titre :** `README shows an MIT badge but the repository contains no LICENSE file`

**Corps :**

```markdown
Hi, and thanks for publishing this component — the LCD, GT911 and USB MSC glue
saved us a lot of bring-up work.

There is a licensing gap that makes it hard to build on, and I think it is
unintentional:

- `README.md` displays a `License: MIT` badge, and the badge links to `LICENSE`.
- There is no `LICENSE` file in the repository.
- GitHub's license API returns 404 for this repo, so no license is detected in
  the repository metadata either.

Under default copyright law, code published without a license is "all rights
reserved": GitHub's Terms of Service let people view and fork it on the
platform, but grant no right to redistribute it or to ship it inside a
derivative work. So despite the badge, downstream projects cannot safely vendor
this component.

Concretely, that is why our project references `PandaTouch_IDF` as a git
submodule and does not redistribute any of its source — we did not want to
assume a license that is only asserted by a badge image. Other projects that do
vendor it (for instance `nomadsgalaxy/Prusa-Connect-Touch`, whose NOTICE file
lists this component as "BigTreeTech, MIT") appear to be relying on the same
assertion.

Would you consider adding the MIT license text as a `LICENSE` file at the
repository root, with the appropriate copyright line? That single file would
make the badge accurate and let the component be used the way the badge already
suggests it can be.

Happy to open a PR with the standard MIT text if that is easier — you would
just need to confirm the copyright holder and year.

For context on what the component enabled: we used it to bring up a BIGTREETECH
K-Touch (the 5-inch model) and were able to confirm that the Panda Touch 7-inch
pinout and timings work on it unchanged, display and GT911 touch alike. That was
previously undocumented publicly. Details here, in case it is useful to you or
to others: <lien vers docs/hardware/pinout.md une fois le dépôt public>
```

---
