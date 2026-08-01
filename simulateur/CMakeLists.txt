cmake_minimum_required(VERSION 3.16)
project(ktouch-simulateur C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# On ne construit ni les démos ni les exemples ni ThorVG : ils ajoutent
# plusieurs centaines de cibles C++ dont ce projet n'utilise rien.
set(LV_CONF_BUILD_DISABLE_DEMOS   1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_EXAMPLES 1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL 1 CACHE INTERNAL "")
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(lvgl)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# La cible `lvgl` compile elle-même les pilotes sous src/drivers/sdl/*.c dès
# que LV_USE_SDL vaut 1 dans lv_conf.h (ces fichiers sont inclus par le glob
# générique de env_support/cmake/custom.cmake, sans condition de build ; seul
# leur contenu est protégé par `#if LV_USE_SDL`). Ces pilotes font
# `#include <SDL2/SDL.h>` : sans ce chemin d'inclusion sur la cible `lvgl`
# elle-même (et pas seulement sur ktouch-sim), la compilation de la
# bibliothèque LVGL échoue avant même d'atteindre notre propre code.
target_include_directories(lvgl PRIVATE ${SDL2_INCLUDE_DIRS})

# plateforme.c (la conversion RSSI -> barres, pure) vit sous firmware/main/core
# avec le reste du socle portable : le simulateur la compile depuis là plutôt
# que d'en garder une copie, pour ne jamais diverger de ce que rend le
# firmware au même signal.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main/core)
# ui/ (descripteur d'écran, pile de navigation) vit sous firmware/main pour la
# même raison : un seul navigation.c, compilé ici comme sur la cible, jamais
# une copie qui pourrait diverger.
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main/ui)
# tuile.{c,h} / progression.{c,h} (tâche 5) : même raison que UI_DIR
# ci-dessus, un seul répertoire de widgets partagés, jamais une copie.
set(WIDGETS_DIR ${UI_DIR}/widgets)
# ecran_accueil.{c,h} (tâche 6) vit sous apps/klipper/ecrans : même raison
# que WIDGETS_DIR ci-dessus, un seul écran compilé ici comme sur la cible et
# dans le harnais de tests hôte (voir host-test/CMakeLists.txt).
set(KLIPPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main/apps/klipper)
set(ECRANS_DIR ${KLIPPER_DIR}/ecrans)
# backend_jouet.{c,h} / ecran_jouet.{c,h} (tache 11) vivent sous exemples/,
# HORS de firmware/main/apps/ : ce n'est pas une application du firmware,
# c'est le modele que --app jouet demontre pour un fork non-Klipper -- voir
# exemples/backend_jouet/README.md.
set(EXEMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../exemples/backend_jouet)
# navigation.c rend esp_err_t (ESP_OK, ESP_ERR_NO_MEM, ...) pour rester le
# même contrat des deux côtés (voir core/backend.h) ; hors ESP-IDF, ce type
# vient du même shim que le harnais de test hôte, sous un répertoire de
# premier niveau (../shim, ni sous host-test/ ni sous simulateur/, pour
# qu'aucun des deux ne semble le posséder) plutôt que d'une deuxième copie
# qui pourrait diverger sur les valeurs numériques des codes d'erreur.
# Écart corrigé après revue : ce shim vivait initialement sous
# host-test/shim/, ce qui faisait dépendre le simulateur — un artefact livré
# — d'un répertoire d'outillage de test sans contrat garanti envers lui.
set(ESP_ERR_SHIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shim)

# cJSON vendorisé : moonraker_parse.c/moonraker_rpc.c ci-dessous en ont besoin
# (tâche 7, --hote) — RÉUTILISE le même vendor/ que host-test/ (voir son
# CMakeLists.txt), jamais une deuxième copie qui pourrait diverger. Même
# CJSON_NESTING_LIMIT=32 que la cible réelle (firmware/CMakeLists.txt) et que
# host-test/, pour que ce backend PC exerce la même configuration que
# l'appareil, pas une plus permissive.
set(CJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../host-test/vendor)
add_library(cjson STATIC ${CJSON_DIR}/cJSON.c)
target_include_directories(cjson PUBLIC ${CJSON_DIR})
target_compile_definitions(cjson PUBLIC CJSON_NESTING_LIMIT=32)

# Tâche 4 (habillage) fait tourner, sur PC, le même protocole que
# core/boucle.c (magasin d'état, liaison, backend factice) via boucle_cycle()
# et source_etat_sim.c (voir son en-tête) : ces quatre fichiers de core/,
# déjà réutilisés tels quels par host-test/CMakeLists.txt, entrent ici pour
# la même raison — une seule implémentation, jamais une copie qui pourrait
# diverger. hote_parse.c (tâche 8) rejoint le lot pour la même raison :
# ecran_configuration.c s'appuie dessus pour valider la saisie de l'écran de
# configuration, jamais un second analyseur.
add_executable(ktouch-sim
    main.c
    afficheur.c
    plateforme_sim.c
    source_etat_sim.c
    source_reglages_sim.c
    moonraker_pc.c
    ${CORE_DIR}/plateforme.c
    ${CORE_DIR}/etat_store.c
    ${CORE_DIR}/liaison.c
    ${CORE_DIR}/backend_factice.c
    ${CORE_DIR}/boucle_cycle.c
    ${CORE_DIR}/hote_parse.c
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
    ${ECRANS_DIR}/ecran_configuration.c
    ${ECRANS_DIR}/ecran_macros.c
    ${EXEMPLES_DIR}/backend_jouet.c
    ${EXEMPLES_DIR}/ecran_jouet.c
    ${KLIPPER_DIR}/moonraker_parse.c
    ${KLIPPER_DIR}/moonraker_rpc.c
    ${KLIPPER_DIR}/klipper_gcode.c
    ${KLIPPER_DIR}/klipper_paliers.c
    ${KLIPPER_DIR}/accueil_choix.c
)
target_include_directories(ktouch-sim PRIVATE . vendor ${CORE_DIR} ${UI_DIR} ${WIDGETS_DIR} ${ECRANS_DIR} ${EXEMPLES_DIR} ${KLIPPER_DIR} ${ESP_ERR_SHIM_DIR} ${CJSON_DIR} ${SDL2_INCLUDE_DIRS})
target_link_libraries(ktouch-sim PRIVATE lvgl cjson ${SDL2_LIBRARIES} m)
target_compile_options(ktouch-sim PRIVATE -Wall -Wextra -Werror)
