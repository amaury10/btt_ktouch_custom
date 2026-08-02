cmake_minimum_required(VERSION 3.16)
project(ktouch_host_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

# AddressSanitizer + UndefinedBehaviorSanitizer sur la cible `tests`
# uniquement (pas sur cJSON, vendorise et non maintenu par nous). Ces deux
# sanitizers detectent mecaniquement des classes de bugs deja rencontrees
# dans ce jalon (conversion float -> uint32_t d'une valeur infinie, free()
# sur un pointeur indetermine) : les activer par defaut protege les taches a
# venir qui allouent ou parsent de l'entree non fiable. Interrupteur prevu
# pour une chaine de compilation qui ne les supporterait pas.
option(KTOUCH_HOST_TEST_SANITIZERS
    "Activer ASan+UBSan sur la cible tests (desactiver: -DKTOUCH_HOST_TEST_SANITIZERS=OFF)"
    ON)

# Les sources testées vivent sous firmware/main/core et doivent rester
# compilables hors ESP-IDF : c'est la contrainte qui garantit qu'elles ne
# dependent pas du materiel.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main/core)
# web_macros.{c,h} (revue tache 1, jalon 3a, fix round 1) : extrait de
# web.c, qui lui NE se compile PAS ici (esp_http_server.h). C'est la seule
# partie de main/ directement au-dessus de core/ qui a besoin d'etre
# testable au harnais hote -- deliberement sans dependance ESP-IDF pour ca,
# voir web_macros.h.
set(MAIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main)
# La couche ui/ (descripteur d'ecran, navigation) doit rester compilable hors
# ESP-IDF au meme titre que core/ : voir firmware/main/ui/navigation.c, qui
# n'inclut que "lvgl.h" et le shim esp_err.h ci-dessous, jamais de header
# FreeRTOS/ESP-IDF.
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main/ui)
# habillage.c (tâche 4) appelle ui_etat_instantane()/ui_commander() (voir
# firmware/main/ui/source_etat.h) : sur PC, ces deux symboles viennent de
# simulateur/source_etat_sim.c, réutilisé tel quel plutôt que dupliqué dans
# host-test/ — même raison que UI_DIR/navigation.c ci-dessus, une seule
# implémentation, jamais deux qui pourraient diverger. plateforme_sim.c
# (heure/batterie/wifi synthétiques) est réutilisé pour la même raison :
# habillage.c les appelle aussi, et core/plateforme.c ne fournit que la
# conversion pure (plateforme_wifi_barres).
set(SIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../simulateur)
# tuile.{c,h} / progression.{c,h} (tâche 5) vivent sous ui/widgets, un
# répertoire de plus sous UI_DIR : même raison que UI_DIR lui-même, une seule
# implémentation compilée ici comme sur la cible et dans le simulateur.
set(WIDGETS_DIR ${UI_DIR}/widgets)
# ecran_accueil.{c,h} (tâche 6) vit sous apps/klipper/ecrans : même raison
# que WIDGETS_DIR ci-dessus, une seule implémentation, jamais une copie.
set(KLIPPER_DIR ${CORE_DIR}/../apps/klipper)
set(ECRANS_DIR ${KLIPPER_DIR}/ecrans)
# backend_jouet.{c,h} / ecran_jouet.{c,h} (tache 11) : voir le meme
# commentaire dans simulateur/CMakeLists.txt -- exemples/, pas apps/, ce
# n'est pas une application du firmware mais le modele que le harnais de
# tests hote exerce pour prouver l'independance de core/ et ui/.
set(EXEMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../exemples/backend_jouet)

# LVGL entre dans le harnais de test hote pour rendre la navigation testable
# sur PC : reutilise le SEUL sous-module et la SEULE lv_conf.h de
# simulateur/, plutot que d'en cloner une deuxieme copie qui pourrait
# diverger silencieusement de ce que rend l'appareil (voir le commentaire de
# tete de simulateur/afficheur.c sur ce risque precis).
set(LV_CONF_BUILD_DISABLE_DEMOS   1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_EXAMPLES 1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL 1 CACHE INTERNAL "")
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../simulateur/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../simulateur/lvgl lvgl-build)

# lv_conf.h (partagee avec le simulateur) active LV_USE_SDL : la cible lvgl
# compile donc elle-meme src/drivers/sdl/*.c, qui font #include <SDL2/SDL.h>
# inconditionnellement (voir le meme commentaire dans
# simulateur/CMakeLists.txt). Sans ce chemin d'inclusion sur la cible lvgl
# elle-meme, la compilation de la bibliotheque echoue avant d'atteindre notre
# propre code — meme si aucun test ici n'appelle jamais le pilote SDL.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
target_include_directories(lvgl PRIVATE ${SDL2_INCLUDE_DIRS})

# Desinfecteurs et LVGL : LVGL est du code tiers, et le compiler sous
# ASan/UBSan allongerait la compilation pour signaler d'eventuels motifs
# internes qui ne sont pas nos defauts. La cible `lvgl` ci-dessus ne recoit
# donc AUCUN des drapeaux KTOUCH_SANITIZE_FLAGS definis plus bas : ceux-ci ne
# sont ajoutes qu'a la cible `tests` (nos propres objets, y compris
# ui/navigation.c). A l'edition de liens, un executable instrumente peut
# sans probleme incorporer une bibliotheque statique non instrumentee tant
# que le runtime des desinfecteurs est present sur le lien final (ce qui est
# le cas ici, KTOUCH_SANITIZE_FLAGS etant aussi passe a target_link_options
# de `tests`). Si l'editeur de liens finissait par s'en plaindre sur une
# future chaine de compilation, le repli documente serait d'activer les
# desinfecteurs aussi sur `lvgl` plutot que de laisser le probleme se
# reproduire en silence.
add_library(cjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
# La cible reelle (firmware/CMakeLists.txt) ramene deja CJSON_NESTING_LIMIT
# de son defaut de 1000 a 32 : une reponse HTTP de 4 Kio remplie de
# crochets/accolades imbriques atteint environ 1000 cadres d'appel et deborde
# la pile de 8 Kio de la tache d'interrogation (core/boucle.c) bien avant que
# la limite par defaut de cJSON ne se declenche, transformant une reponse
# hostile ou corrompue en panique au lieu d'un simple echec d'analyse propre
# (voir le commentaire de firmware/CMakeLists.txt pour le detail). Ce
# garde-fou existait donc deja sur cible, mais RIEN ne l'exerçait cote hote :
# le cJSON vendorise ici tournait avec son defaut de 1000, et aucun test ne
# construisait une entree assez profonde pour le declencher. Reprendre ici la
# meme valeur (32, plutot que le defaut vendorise) rend ce garde-fou
# reellement testable par un test hote court (voir test_moonraker_parse.c),
# et garantit que le harnais hote exerce la MEME configuration que la cible,
# pas une plus permissive qui masquerait une regression. `#ifndef` dans
# cJSON.h/.c : doit etre defini AVANT que vendor/cJSON.c ne soit compile,
# donc sur la cible `cjson` elle-meme, pas sur `tests` qui ne fait qu'appeler
# l'API deja compilee.
target_compile_definitions(cjson PUBLIC CJSON_NESTING_LIMIT=32)

add_executable(tests
    tests/main.c
    tests/test_harnais.c
    tests/test_contrat.c
    tests/test_moonraker_parse.c
    tests/test_moonraker_rpc.c
    tests/test_fixtures_moonraker.c
    tests/test_moonraker_boite.c
    tests/test_etat_store.c
    tests/test_liaison.c
    tests/test_backend_factice.c
    tests/test_hote_parse.c
    tests/test_boucle_cycle.c
    tests/test_plateforme.c
    tests/test_navigation.c
    tests/test_habillage.c
    tests/test_widgets.c
    tests/test_ecran_accueil.c
    tests/test_ecran_deplacer.c
    tests/test_ecran_accueil_hub.c
    tests/test_ecran_temperatures.c
    tests/test_ecran_extruder.c
    tests/test_ecran_ventilateurs.c
    tests/test_ecran_reglage_fin.c
    tests/test_ecran_zcalibrate.c
    tests/test_ecran_niveau_lit.c
    tests/test_ecran_limites.c
    tests/test_ecran_retraction.c
    tests/test_ecran_stub.c
    tests/test_accueil_choix.c
    tests/test_clavier.c
    tests/test_ecran_configuration.c
    tests/test_commandes.c
    tests/test_jouet.c
    tests/test_web_macros.c
    tests/test_ecran_macros.c
    tests/test_ecran_fichiers.c
    tests/test_ecran_reglages_wifi.c
    tests/wifi_mock.c
    tests/test_klipper_gcode.c
    tests/test_klipper_paliers.c
    tests/test_selecteur_pas.c
    tests/test_selecteur_choix.c
    tests/test_rail.c
    tests/test_integration_rail.c
    tests/test_bascule_accueil.c
    ${MAIN_DIR}/web_macros.c
    ${KLIPPER_DIR}/moonraker_parse.c
    ${KLIPPER_DIR}/moonraker_rpc.c
    ${KLIPPER_DIR}/klipper_fichiers.c
    ${KLIPPER_DIR}/moonraker_boite.c
    ${KLIPPER_DIR}/klipper_gcode.c
    ${KLIPPER_DIR}/klipper_paliers.c
    ${KLIPPER_DIR}/accueil_choix.c
    ${KLIPPER_DIR}/rail_actions.c
    ${CORE_DIR}/etat_store.c
    ${CORE_DIR}/liaison.c
    ${CORE_DIR}/backend_factice.c
    ${CORE_DIR}/hote_parse.c
    ${CORE_DIR}/boucle_cycle.c
    ${CORE_DIR}/plateforme.c
    ${UI_DIR}/navigation.c
    ${UI_DIR}/habillage.c
    ${WIDGETS_DIR}/tuile.c
    ${WIDGETS_DIR}/progression.c
    ${WIDGETS_DIR}/selecteur_pas.c
    ${WIDGETS_DIR}/selecteur_choix.c
    ${WIDGETS_DIR}/rail.c
    ${WIDGETS_DIR}/clavier.c
    ${WIDGETS_DIR}/confirmation.c
    ${ECRANS_DIR}/ecran_accueil.c
    ${ECRANS_DIR}/ecran_deplacer.c
    ${ECRANS_DIR}/ecran_accueil_hub.c
    ${ECRANS_DIR}/ecran_temperatures.c
    ${ECRANS_DIR}/ecran_extruder.c
    ${ECRANS_DIR}/ecran_ventilateurs.c
    ${ECRANS_DIR}/ecran_reglage_fin.c
    ${ECRANS_DIR}/ecran_zcalibrate.c
    ${ECRANS_DIR}/ecran_niveau_lit.c
    ${ECRANS_DIR}/ecran_limites.c
    ${ECRANS_DIR}/ecran_retraction.c
    ${ECRANS_DIR}/ecran_stub.c
    ${ECRANS_DIR}/ecran_configuration.c
    ${ECRANS_DIR}/ecran_macros.c
    ${ECRANS_DIR}/ecran_fichiers.c
    ${ECRANS_DIR}/ecran_reglages_wifi.c
    ${SIM_DIR}/source_etat_sim.c
    ${SIM_DIR}/plateforme_sim.c
    ${SIM_DIR}/source_reglages_sim.c
    ${EXEMPLES_DIR}/backend_jouet.c
    ${EXEMPLES_DIR}/ecran_jouet.c
)
target_link_libraries(tests PRIVATE cjson lvgl m)
# Le shim esp_err.h vit sous un repertoire de premier niveau (../shim),
# neutre entre host-test/ et simulateur/ : les deux le consomment (voir
# simulateur/CMakeLists.txt), et aucun des deux ne le possede plus.
target_include_directories(tests PRIVATE ${MAIN_DIR} ${CORE_DIR} ${KLIPPER_DIR} ${ECRANS_DIR} ${UI_DIR} ${WIDGETS_DIR} ${SIM_DIR} ${EXEMPLES_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../shim)
# -Werror ne s'applique qu'a notre propre code : cJSON est vendorise, et le
# coupler a nos avertissements ferait echouer le build sur un futur
# compilateur plus strict pour un warning dans du code qu'on ne modifie pas.
target_compile_options(tests PRIVATE -Wall -Wextra -Werror)

# Chemin ABSOLU (fige a la configuration CMake) vers les fixtures
# enregistrees contre un vrai Moonraker (tache 4, jalon 3a) : voir
# tests/test_fixtures_moonraker.c, qui les relit avec un simple fopen().
# Choix documente (le brief de la tache laissait le choix) : un define
# absolu plutot qu'une copie des fichiers a cote du binaire (etape de build
# en plus, chemins relatifs fragiles selon le repertoire d'execution) --
# comme le harnais est toujours construit depuis CE depot, un chemin absolu
# fige a la configuration reste valide quel que soit le repertoire de build
# ou le repertoire courant au moment de lancer `tests`.
target_compile_definitions(tests PRIVATE
    KTOUCH_FIXTURES_MOONRAKER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/moonraker")

if(KTOUCH_HOST_TEST_SANITIZERS)
    # -fno-omit-frame-pointer : piles d'appel lisibles dans les rapports.
    # -fno-sanitize-recover=all : un probleme UBSan interrompt (abort) le
    # programme au lieu d'imprimer un avertissement et de continuer — sinon
    # il defile dans la sortie et personne ne le remarque, ce qui annule
    # l'interet de l'avoir active. Les options doivent figurer a la
    # compilation ET a l'edition de liens.
    #
    # float-cast-overflow est ajoute explicitement : avec gcc (contrairement
    # a clang), ce check n'est PAS inclus dans le groupe "undefined" — verifie
    # empiriquement avec gcc 14.2 (un cast (uint32_t) d'un float infini passe
    # inapercu avec -fsanitize=undefined seul). Comme c'est precisement la
    # classe de bug deja trouvee dans ce jalon (conversion d'un temps restant
    # potentiellement infini), l'omettre viderait ASan+UBSan de leur interet
    # pour ce cas precis.
    set(KTOUCH_SANITIZE_FLAGS -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_compile_options(tests PRIVATE ${KTOUCH_SANITIZE_FLAGS})
    target_link_options(tests PRIVATE ${KTOUCH_SANITIZE_FLAGS})
endif()

enable_testing()
add_test(NAME suite COMMAND tests)
