idf_component_register(
    SRCS "app_main.c" "rescue.c" "wifi.c" "netlog.c" "web.c"
         "core/etat_store.c" "core/liaison.c" "core/backend_factice.c" "core/hote_parse.c" "core/reglages.c"
    INCLUDE_DIRS "." "core"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server app_update esp_timer esp_partition PandaTouch_IDF
)
