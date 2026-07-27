cmake_minimum_required(VERSION 3.16)
project(ktouch_host_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

# Les sources testées vivent sous firmware/main/core et doivent rester
# compilables hors ESP-IDF : c'est la contrainte qui garantit qu'elles ne
# dependent pas du materiel.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main/core)

add_library(cjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

add_executable(tests
    tests/main.c
    tests/test_harnais.c
    tests/test_contrat.c
)
target_link_libraries(tests PRIVATE cjson)
target_include_directories(tests PRIVATE ${CORE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
# -Werror ne s'applique qu'a notre propre code : cJSON est vendorise, et le
# coupler a nos avertissements ferait echouer le build sur un futur
# compilateur plus strict pour un warning dans du code qu'on ne modifie pas.
target_compile_options(tests PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME suite COMMAND tests)
