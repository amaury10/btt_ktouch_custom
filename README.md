# BTT K-Touch Custom

Firmware ouvert et outillage de rétro-ingénierie pour la **BIGTREETECH K-Touch**,
un écran tactile ESP32-S3 de 5 pouces dont le développement a été arrêté par son
fabricant (dernière version publiée : `v1.1.0`, novembre 2024, qui s'identifie
elle-même comme une beta).

Le projet poursuit deux buts sur une base technique commune : redonner un
firmware vivant et compilable aux possesseurs de l'appareil, et détourner
celui-ci pour piloter un tracker astrophotographique.

## État

Jalon 1 en cours : preuve de vie d'un firmware maison dans le slot OTA `app1`.
Rien n'est encore utilisable au quotidien.

## Avertissement

Reprogrammer l'appareil se fait à vos risques. Cela dit, la démarche est conçue
pour être réversible : le firmware d'origine reste intact dans le slot `app0`, et
les outils de ce dépôt vérifient chaque sauvegarde avant toute écriture.

## Licence

MIT pour le code de ce dépôt. Le support matériel provient du composant
[`bigtreetech/PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF),
référencé en sous-module et non redistribué ici.
