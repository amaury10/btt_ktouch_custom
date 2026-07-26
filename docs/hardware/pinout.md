# Pinout de la BIGTREETECH K-Touch — vérifié sur matériel

**Statut : affichage et tactile confirmés sur matériel le 26 juillet 2026.**

## Ce que ce document apporte

Le pinout du panneau de la K-Touch 5 pouces n'était documenté nulle part
publiquement. Le seul disponible était celui du **Panda Touch 7 pouces**, publié
par BIGTREETECH dans son composant `PandaTouch_IDF`, et le projet
`nomadsgalaxy/Prusa-Connect-Touch` notait dans son README que la K-Touch « is the
same family but **may differ on a few panel GPIOs or timings** ».

**Cette réserve est levée : le pinout du Panda Touch 7 pouces fonctionne tel quel
sur la K-Touch 5 pouces**, affichage comme tactile, aux valeurs exactes
reproduites ci-dessous et sans aucune adaptation.

## Comment la vérification a été faite

Un firmware minimal construit sur `bigtreetech/PandaTouch_IDF` (commit
`396eaba`) a été installé dans le slot OTA `app1` d'une K-Touch tournant sous son
firmware d'origine `V1.0.0`, via le mécanisme de mise à jour du fabricant. Il
affiche une mire choisie pour que chaque défaut de câblage se voie.

| Observation | Ce qu'elle démontre |
|---|---|
| Bandes rouge, vert, bleu, blanc **dans cet ordre** | Les 16 broches de données sont dans le bon ordre ; aucune inversion de canal |
| Quatre repères visibles **aux quatre coins** | Les 800 × 480 sont balayés en entier ; résolution et porches corrects |
| Texte net, sans décalage ni déchirement | Synchronisation correcte ; mode DE opérationnel |
| Image **stable, sans scintillement** | Horloge pixel et porches viables à 23 MHz |
| Rétroéclairage allumé | `GPIO21` et la configuration LEDC corrects |

La réversibilité a été démontrée de deux façons distinctes, et il vaut la peine
de ne pas les confondre. Lors d'essais antérieurs où le WiFi ne s'associait pas,
l'appareil est **revenu seul** au firmware d'origine, deux fois, par le mécanisme
de sauvetage automatique embarqué. Puis, l'essai concluant terminé, il y est
retourné **sur commande** via la route `/revert`. Le premier cas prouve que le
retour survit à une panne ; le second, qu'il est disponible à la demande.

## Panneau LCD — RGB parallèle 16 bits, mode DE

`HSYNC` et `VSYNC` ne sont pas routés : le panneau fonctionne en mode DE.

| Signal | GPIO |
|---|---|
| PCLK | `5` |
| DE | `38` |
| Reset | `46` |
| Rétroéclairage (PWM LEDC) | `21` |
| HSYNC | non routé |
| VSYNC | non routé |

Broches de données, dans l'ordre `DATA0` à `DATA15` :

```
17, 18, 48, 47, 39, 11, 12, 13, 14, 15, 16, 6, 7, 8, 9, 10
```

## Timings validés

| Paramètre | Valeur |
|---|---|
| Résolution | 800 × 480 |
| Horloge pixel | **23 MHz** |
| HSYNC : impulsion / back porch / front porch | 4 / 8 / 8 |
| VSYNC : impulsion / back porch / front porch | 4 / 16 / 16 |

> Le composant amont définit aussi `PT_LCD_PCLK_HZ_MIN` à 14 MHz. La valeur de
> 23 MHz est celle du dépôt officiel de BTT et c'est celle qui a été validée ici.
> Attention si vous partez d'une copie tierce : celle vendue avec
> Prusa-Connect-Touch a été ramenée à 17 MHz par ses auteurs, pour réduire la
> contention sur le bus PSRAM. Les deux fonctionnent probablement, mais seule la
> valeur de 23 MHz est confirmée par ce test.

## Rétroéclairage

Piloté en PWM par le périphérique LEDC : `LEDC_TIMER_1`, canal `LEDC_CHANNEL_0`,
mode basse vitesse, résolution 11 bits, fréquence 30 kHz.

## Tactile GT911 — confirmé

| Signal | GPIO |
|---|---|
| I²C SCL | `1` |
| I²C SDA | `2` |
| Reset | `41` |
| Interruption | `40` |

Registres : état `0x814E`, premier point `0x814F`, jusqu'à 5 points simultanés.
Le contrôleur répond à l'adresse I²C `0x5D`.

Traces d'initialisation obtenues sur l'appareil :

```
PandaTouch::Touch: ACK 0x5D (no reset)
PandaTouch::Touch: STATUS=0x00
PandaTouch::Touch: PT_GT911 ready @ 0x5D
PandaTouch::LVGL_Touch: PT GT911 LVGL indev registered (800x480 touch -> 800x480 disp)
```

### Orientation et échelle — vérifiées

Les quatre repères de la mire ont leurs centres en `(12,12)`, `(788,12)`,
`(788,468)` et `(12,468)`. Appuyés dans le sens des aiguilles d'une montre en
partant du coin supérieur gauche, ils ont remonté :

| Ordre d'appui | Coin physique | Coordonnées lues |
|---|---|---|
| 1 | haut-gauche | `(27, 23)` |
| 2 | haut-droite | `(784, 21)` |
| 3 | bas-droite | `(777, 459)` |
| 4 | bas-gauche | `(27, 460)` |

La correspondance est directe : **aucune rotation, aucun miroir, aucune
inversion d'axe**, et l'échelle est correcte sur les deux axes. Les écarts d'une
dizaine de pixels par rapport aux centres théoriques correspondent à la surface
de contact d'un doigt.

C'est un point qui méritait vérification et non déduction : l'orientation du
tactile se règle dans le GT911, indépendamment du panneau. Un pinout d'affichage
correct n'implique en rien un tactile correctement orienté.

## Stabilité

L'appareil a fonctionné **49 minutes** sous ce firmware sans un seul redémarrage
— compteur de démarrages resté à 1, tas libre inchangé à l'octet près (7 399 519)
entre deux relevés espacés d'une demi-heure. Ni chien de garde, ni panique, ni
fuite mémoire observable.

## Sources

Les valeurs de broches proviennent de `bigtreetech/PandaTouch_IDF`, publié par
BIGTREETECH. Ce dépôt ne contient aucun fichier de licence ; il est référencé ici
en sous-module et n'est pas redistribué. Les numéros de broches et les timings
sont des faits matériels, non protégeables — c'est leur **vérification sur la
K-Touch 5 pouces** qui constitue l'apport de ce document.
