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

add_executable(ktouch-sim main.c afficheur.c)
target_include_directories(ktouch-sim PRIVATE . vendor ${SDL2_INCLUDE_DIRS})
target_link_libraries(ktouch-sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
target_compile_options(ktouch-sim PRIVATE -Wall -Wextra -Werror)
