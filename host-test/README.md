# Harnais de test hôte

## À quoi ça sert

Ce répertoire fait tourner, sur PC, les tests unitaires du code « non
visuel » du firmware : analyseurs JSON, store d'état, machine à états de
connexion (`firmware/main/core/`). Aucun de ces modules ne touche au
matériel (écran, Wi-Fi, GPIO) — ce sont des fonctions pures qui prennent du
JSON ou des structures en entrée et produisent des structures en sortie.

**Pourquoi `core/` doit rester compilable hors ESP-IDF.** C'est ce qui rend
ce harnais possible. Si `core/` en venait à dépendre d'un en-tête ESP-IDF ou
d'une fonction spécifique au matériel, ces tests ne compileraient plus sur
PC et il faudrait revenir à flasher l'appareil, le redémarrer et lire le
journal à distance pour vérifier le moindre changement — plusieurs
dizaines de secondes par itération au lieu d'une fraction de seconde. Les
analyseurs JSON sont l'endroit où vivront la plupart des bugs du jalon 2a,
car c'est là qu'on interprète le JSON parfois surprenant renvoyé par
Moonraker : les tester vite est ce qui permet de les tester souvent.

Concrètement : rien sous `host-test/` ne doit inclure `esp_*.h`,
`freertos/*.h`, ni aucun en-tête fourni uniquement par le SDK ESP-IDF. Si un
fichier de `core/` a besoin d'un en-tête pareil, c'est le signe qu'il fait
trop de choses et qu'il faut en extraire la partie pure et testable.

## Paquets à installer sous WSL

Ce harnais est pensé pour tourner sous WSL (Debian), pas directement sous
Windows. Il lui faut :

```
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config
```

(`build-essential` fournit `gcc` ; les autres paquets sont listés
explicitement même si `cmake` et `ninja-build` dépendent déjà d'une chaîne
de compilation, pour rendre la liste copiable telle quelle.)

## Lancer la suite

Depuis un shell Windows (PowerShell), avec le dépôt visible sous
`/mnt/<lettre>/...` dans WSL :

```powershell
wsl -d Debian -- sh "/mnt/e/Dev/BTT KTouch Custom/host-test/run.sh"
```

Ou, une fois dans un shell WSL, depuis la racine du dépôt :

```sh
./host-test/run.sh
```

Le bit d'exécution est suivi par Git (mode `100755` dans l'arbre), donc
`./run.sh` fonctionne directement après un `git clone`. `sh run.sh` reste
une alternative valide si jamais le bit d'exécution ne survit pas à un
transfert (partage réseau, archive zip, etc.).

`run.sh` configure un dossier `build/` avec CMake + Ninja, compile, puis
lance l'exécutable `tests`. Il rend le code de sortie de `tests` : non nul
dès qu'une vérification échoue.

Sortie attendue (suite actuelle, avant que les tâches suivantes n'ajoutent
les leurs) :

```
suite : harnais

3 verification(s), 0 echec(s)
```

## Sanitizers (ASan + UBSan)

La cible `tests` compile par défaut avec AddressSanitizer et
UndefinedBehaviorSanitizer (`-fsanitize=address,undefined,float-cast-overflow
-fno-omit-frame-pointer -fno-sanitize-recover=all`). cJSON (vendorisé) n'est
**pas** instrumenté : ce n'est pas du code qu'on maintient, et coupler ses
éventuels avertissements à notre build le ferait échouer pour un problème
qu'on ne peut pas corriger.

Ce que ça achète concrètement : trois défauts déjà trouvés dans ce jalon
appartiennent à des classes que ces sanitizers détectent mécaniquement — une
conversion `float → uint32_t` d'une valeur potentiellement infinie
(`float-cast-overflow`), un `free()` sur un pointeur indéterminé laissé par
une initialisation en échec (heap-use-after-free / invalid-free), et un NaN
qui se glisse sans bruit dans une comparaison. Les repérer à la revue humaine
est plus lent et moins fiable qu'un outil qui fait échouer le build.
`-fno-sanitize-recover=all` est ce qui rend ça non contournable : sans cette
option, UBSan se contente d'imprimer un avertissement et continue, et un
avertissement qui défile dans la sortie ne sera pas vu.

**Note gcc :** avec gcc (contrairement à clang), `float-cast-overflow` n'est
*pas* inclus dans le groupe `-fsanitize=undefined` — c'est pourquoi il est
listé explicitement ci-dessus. Vérifié empiriquement avec gcc 14.2 : un cast
`(uint32_t)` d'un float infini passe inaperçu avec `-fsanitize=undefined`
seul.

Interrupteur : l'option CMake `KTOUCH_HOST_TEST_SANITIZERS` (défaut `ON`),
pour une chaîne de compilation qui ne supporterait pas ces sanitizers. Pour
la désactiver :

```sh
cmake -S host-test -B host-test/build -G Ninja -DKTOUCH_HOST_TEST_SANITIZERS=OFF
cmake --build host-test/build
./host-test/build/tests
```

Une fois la valeur `OFF` écrite dans le cache CMake de `host-test/build/`,
elle survit aux reconfigurations ultérieures (y compris via `run.sh`, qui
appelle `cmake` sans repréciser l'option) tant que ce dossier `build/` n'est
pas supprimé.

## Le micro-cadre de test

Pas de framework externe (pas d'Unity vendorisé, pas de CTest au-delà d'un
`add_test` minimal) : trois macros dans `tests/petit_test.h`
(`VERIFIER`, `VERIFIER_FLOAT`, `VERIFIER_TEXTE`) suffisent pour tester des
fonctions pures. Chaque tâche suivante ajoute un fichier `tests/test_*.c`
et une fonction `void suite_xxx(void)` appelée depuis `tests/main.c`.

## Fins de ligne

Ce dépôt a `core.autocrlf=true` côté Windows ; sans précaution, un
`git checkout` réintroduirait des fins de ligne CRLF dans `run.sh`, ce que
`sh` sous WSL refuse d'exécuter. `host-test/.gitattributes` force `eol=lf`
sur tout ce répertoire pour éviter ce piège.
