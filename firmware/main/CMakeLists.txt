idf_component_register(
    SRCS "app_main.c" "rescue.c" "wifi.c" "netlog.c" "web.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server app_update esp_timer esp_partition PandaTouch_IDF
)
