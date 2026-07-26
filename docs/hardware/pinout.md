# Pinout de la BIGTREETECH K-Touch — vérifié sur matériel

**Statut : affichage confirmé le 26 juillet 2026. Tactile non encore vérifié.**

## Ce que ce document apporte

Le pinout du panneau de la K-Touch 5 pouces n'était documenté nulle part
publiquement. Le seul disponible était celui du **Panda Touch 7 pouces**, publié
par BIGTREETECH dans son composant `PandaTouch_IDF`, et le projet
`nomadsgalaxy/Prusa-Connect-Touch` notait dans son README que la K-Touch « is the
same family but **may differ on a few panel GPIOs or timings** ».

**Cette réserve est levée pour la partie affichage : le pinout du Panda Touch
7 pouces fonctionne tel quel sur la K-Touch 5 pouces**, aux valeurs exactes
reproduites ci-dessous, sans aucune adaptation.

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

L'appareil est ensuite revenu de lui-même au firmware d'origine par le mécanisme
de sauvetage automatique, ce qui confirme que la manipulation est réversible.

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

## Tactile GT911 — **non encore vérifié**

Valeurs héritées du Panda Touch, **à confirmer** :

| Signal | GPIO |
|---|---|
| I²C SCL | `1` |
| I²C SDA | `2` |
| Reset | `41` |
| Interruption | `40` |

Registres : état `0x814E`, premier point `0x814F`, jusqu'à 5 points simultanés.

La vérification demande de lire les coordonnées remontées lors d'un appui, ce qui
n'a pas encore été possible : le journal du firmware n'est accessible que par le
réseau, et le WiFi ne s'est pas connecté lors du premier essai. Le firmware
n'ayant pas signalé de GT911 muet, rien ne laisse présager de problème — mais
tant que des coordonnées cohérentes n'ont pas été observées, ce tableau reste une
hypothèse et non un résultat.

## Ce qui reste ouvert

L'orientation et la mise à l'échelle du tactile ne sont pas vérifiées non plus :
des coordonnées inversées ou en miroir se règlent dans le GT911 et non dans le
panneau, ce sont deux réglages indépendants.

## Sources

Les valeurs de broches proviennent de `bigtreetech/PandaTouch_IDF`, publié par
BIGTREETECH. Ce dépôt ne contient aucun fichier de licence ; il est référencé ici
en sous-module et n'est pas redistribué. Les numéros de broches et les timings
sont des faits matériels, non protégeables — c'est leur **vérification sur la
K-Touch 5 pouces** qui constitue l'apport de ce document.
