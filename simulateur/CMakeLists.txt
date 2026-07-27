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

add_executable(ktouch-sim main.c afficheur.c plateforme_sim.c ${CORE_DIR}/plateforme.c ${UI_DIR}/navigation.c)
target_include_directories(ktouch-sim PRIVATE . vendor ${CORE_DIR} ${UI_DIR} ${ESP_ERR_SHIM_DIR} ${SDL2_INCLUDE_DIRS})
target_link_libraries(ktouch-sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
target_compile_options(ktouch-sim PRIVATE -Wall -Wextra -Werror)
