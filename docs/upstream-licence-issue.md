# Issue à ouvrir chez BIGTREETECH

**Dépôt cible :** https://github.com/bigtreetech/PandaTouch_IDF/issues/new

Le texte ci-dessous est prêt à être copié. Il est en anglais, langue d'usage sur
ce dépôt. À poster manuellement : ouvrir une issue chez un tiers engage ton
identité GitHub, ce n'est pas une action que l'outillage doit faire à ta place.

Une fois postée, ajouter le lien de l'issue dans la section Licence du `README.md`.

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

## Pourquoi cette démarche

Ce n'est pas une formalité. La licence du composant est le point qui a
déterminé l'architecture du dépôt : sans fichier `LICENSE`, il est référencé en
sous-module et jamais recopié, ce qui évite toute redistribution. Si BTT ajoute
le fichier, cette contrainte tombe et le composant pourra être intégré
normalement — voire forké pour y corriger les défauts relevés en chemin, comme
les `ESP_ERROR_CHECK` de `pt_backlight_init()` qui rendent une défaillance LEDC
fatale pour l'application appelante.

Si BTT ne répond pas, le repli reste la réécriture propre du support matériel :
les numéros de broches et les timings sont des faits, non protégeables, et le
reste n'est qu'une couche de collage au-dessus de `esp_lcd_rgb_panel` et
`esp_lcd_touch_gt911`, tous deux publiés par Espressif sous Apache-2.0.
