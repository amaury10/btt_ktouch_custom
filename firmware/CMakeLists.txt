cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ktouch-custom)

# Le composant "json" d'ESP-IDF (cJSON) limite par defaut la profondeur
# d'imbrication a 1000 (CJSON_NESTING_LIMIT) — assez pour eviter une
# recursion infinie, mais pas pour proteger une pile FreeRTOS de 8 Kio comme
# celle de la tache d'interrogation (core/boucle.c) : un corps de reponse
# HTTP de 4 Kio rempli de crochets/accolades imbriques atteint environ 1000
# cadres d'appel et deborde cette pile bien avant que la limite de cJSON ne
# se declenche, transformant une reponse hostile ou corrompue en panique au
# lieu d'un simple echec d'analyse propre. Ramenee ici a 32 : tres au-dessus
# de ce que /printer/objects/query renvoie jamais (moonraker_parse.c), tres
# en-deca de ce qu'une pile de 8 Kio peut absorber.
idf_component_get_property(ktouch_json_lib json COMPONENT_LIB)
target_compile_definitions(${ktouch_json_lib} PRIVATE "CJSON_NESTING_LIMIT=32")
