cmake_minimum_required(VERSION 3.16)
project(ktouch_host_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g")

# Les sources testées vivent sous firmware/main/core et doivent rester
# compilables hors ESP-IDF : c'est la contrainte qui garantit qu'elles ne
# dependent pas du materiel.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main/core)

add_library(cjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

add_executable(tests
    tests/main.c
    tests/test_harnais.c
)
target_link_libraries(tests PRIVATE cjson)
target_include_directories(tests PRIVATE ${CORE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_test(NAME suite COMMAND tests)
