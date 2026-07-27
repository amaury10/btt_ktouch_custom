idf_component_register(
    SRCS "app_main.c" "rescue.c" "wifi.c" "netlog.c" "web.c"
         "core/etat_store.c" "core/liaison.c" "core/backend_factice.c" "core/hote_parse.c" "core/reglages.c"
         "core/boucle.c" "core/boucle_cycle.c" "apps/klipper/moonraker_parse.c" "apps/klipper/backend_moonraker.c"
    INCLUDE_DIRS "." "core" "apps/klipper"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_http_client json app_update esp_timer esp_partition PandaTouch_IDF
)
