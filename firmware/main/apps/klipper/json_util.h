/* json_util.h — petits utilitaires JSON purs, sans dépendance à cJSON, pour
 * les cas où CONSTRUIRE une chaîne JSON (pas la lire) suffit à justifier une
 * fonction dédiée plutôt qu'un arbre cJSON complet.
 *
 * Contexte (feature Console gcode, tâche A) : la console envoie du texte
 * LIBRE tapé par l'utilisateur au clavier tactile, embarqué tel quel dans
 * `{"script":"<texte>"}` avant transmission (voir ecran_console.c, tâche B).
 * Un utilisateur peut taper des guillemets ou des antislashs (rares en gcode
 * mais pas interdits, ex. un commentaire ";  dit \"bonjour\"") -- sans
 * échappement, la chaîne construite ne serait plus du JSON valide et
 * casserait le parsing côté Moonraker (ou pire, permettrait d'injecter un
 * champ JSON supplémentaire). Contrairement aux noms de macros/prises Power,
 * qui sont des identifiants CONTRAINTS (voir moonraker_rpc.h), la saisie
 * console n'a aucune contrainte de forme : c'est le seul point d'entrée de ce
 * jalon où l'échappement JSON est un vrai point de sécurité, pas une
 * formalité. */
#pragma once

#include <stddef.h>

/* Échappe `src` pour l'insérer tel quel entre guillemets JSON (l'appelant
 * fournit les guillemets ouvrant/fermant, cette fonction ne les ajoute pas --
 * cohérent avec les `"{\"script\":\"%s\"}"` déjà construits à la main dans ce
 * dépôt, voir backend_moonraker.c). Échappe au minimum :
 *   - `"`  -> `\"`
 *   - `\`  -> `\\`
 *   - tout octet de contrôle < 0x20 (0x00..0x1F, `\n`/`\r`/`\t` compris) ->
 *     `\uXXXX` (échappement Unicode 4 chiffres hexadécimaux minuscules,
 *     forme UNIFORME plutôt que les raccourcis `\n`/`\t`/`\r` : un seul
 *     chemin de code à borner et à tester, et un résultat tout aussi valide
 *     en JSON -- choix documenté, l'autre option acceptable était le
 *     remplacement par un espace).
 * Tout autre octet (>= 0x20, y compris les octets de continuation UTF-8
 * 0x80-0xFF) est recopié tel quel : cette fonction échappe la SYNTAXE JSON,
 * elle ne valide ni ne retouche l'encodage du texte source.
 *
 * Bornage STRICT façon snprintf() : `dest` ne reçoit jamais plus de
 * `dest_n - 1` octets utiles plus le `'\0'` final (jamais de débordement,
 * jamais de séquence d'échappement coupée en deux -- chaque groupe de sortie
 * ci-dessus, ex. les 6 octets d'un `\uXXXX`, est écrit atomiquement ou pas du
 * tout). `dest`/`dest_n` peuvent être `NULL`/0 : dans ce cas rien n'est
 * écrit, seule la longueur nécessaire est calculée -- même usage de sonde que
 * `snprintf(NULL, 0, ...)`.
 *
 * Rend le nombre d'octets qui AURAIENT été écrits (hors `'\0'` final) si
 * `dest` avait été assez grand -- même convention que la valeur de retour de
 * `snprintf()`, déjà utilisée partout ailleurs dans ce dépôt pour détecter
 * une troncature (comparer le retour à `dest_n` : `retour >= dest_n` =>
 * troncature, voir rpc_construire_requete() dans moonraker_rpc.c). `src ==
 * NULL` est traité comme une chaîne vide (rend 0, `dest[0] = '\0'` si la
 * place le permet) -- jamais de déréférencement NULL. */
size_t json_echapper_chaine(char *dest, size_t dest_n, const char *src);
