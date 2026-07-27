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

add_library(cjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

add_executable(tests
    tests/main.c
    tests/test_harnais.c
    tests/test_contrat.c
    tests/test_moonraker_parse.c
    tests/test_etat_store.c
    tests/test_liaison.c
    ${CORE_DIR}/../apps/klipper/moonraker_parse.c
    ${CORE_DIR}/etat_store.c
    ${CORE_DIR}/liaison.c
)
target_link_libraries(tests PRIVATE cjson)
target_include_directories(tests PRIVATE ${CORE_DIR} ${CORE_DIR}/../apps/klipper ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/shim)
# -Werror ne s'applique qu'a notre propre code : cJSON est vendorise, et le
# coupler a nos avertissements ferait echouer le build sur un futur
# compilateur plus strict pour un warning dans du code qu'on ne modifie pas.
target_compile_options(tests PRIVATE -Wall -Wextra -Werror)

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
