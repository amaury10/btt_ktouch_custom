#include "habillage.h"
#include "liaison.h"
#include "petit_test.h"

void suite_habillage(void)
{
    printf("suite : habillage\n");
    /* connexion */ VERIFIER_TEXTE(habillage_texte_liaison(LIAISON_CONNEXION), "connecting");
    /* en ligne */ VERIFIER_TEXTE(habillage_texte_liaison(LIAISON_EN_LIGNE), "online");
    /* degradee */ VERIFIER_TEXTE(habillage_texte_liaison(LIAISON_DEGRADEE), "unstable");
    /* hors ligne */ VERIFIER_TEXTE(habillage_texte_liaison(LIAISON_HORS_LIGNE), "offline");
    /* Un état inconnu ne doit jamais rendre NULL : lv_label_set_text(NULL)
     * déréférence et fait tomber l'interface. */
    /* etat inconnu rend un texte */ VERIFIER(habillage_texte_liaison((liaison_etat_t)99) != NULL);

    /* Règle 5.3 : périmé dès qu'on n'est plus en ligne, y compris pendant la
     * connexion initiale — afficher des zéros en blanc franc pendant les
     * premières secondes ferait lire « buse à 0 C » comme une mesure. */
    /* en ligne : donnees fraiches */ VERIFIER(!habillage_donnees_perimees(LIAISON_EN_LIGNE));
    /* degradee : donnees perimees */ VERIFIER(habillage_donnees_perimees(LIAISON_DEGRADEE));
    /* hors ligne : donnees perimees */ VERIFIER(habillage_donnees_perimees(LIAISON_HORS_LIGNE));
    /* connexion : donnees perimees */ VERIFIER(habillage_donnees_perimees(LIAISON_CONNEXION));

    /* couleurs distinctes en ligne / hors ligne */ VERIFIER(habillage_couleur_liaison(LIAISON_EN_LIGNE) != habillage_couleur_liaison(LIAISON_HORS_LIGNE));
}
