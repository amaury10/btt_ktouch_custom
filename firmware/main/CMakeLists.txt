idf_component_register(
    SRCS "app_main.c" "rescue.c" "wifi.c" "netlog.c" "web.c" "web_macros.c"
         "core/etat_store.c" "core/liaison.c" "core/backend_factice.c" "core/hote_parse.c" "core/reglages.c"
         "core/boucle.c" "core/boucle_cycle.c" "core/plateforme.c" "core/plateforme_esp.c"
         "apps/klipper/moonraker_parse.c" "apps/klipper/moonraker_rpc.c" "apps/klipper/moonraker_boite.c"
         "apps/klipper/moonraker_ws.c" "apps/klipper/backend_moonraker.c"
         "apps/klipper/ecrans/ecran_accueil.c" "apps/klipper/ecrans/ecran_configuration.c"
         "ui/navigation.c" "ui/habillage.c" "ui/source_etat_esp.c" "ui/source_reglages_esp.c"
         "ui/widgets/tuile.c" "ui/widgets/progression.c" "ui/widgets/clavier.c" "ui/widgets/confirmation.c"
    INCLUDE_DIRS "." "core" "apps/klipper" "apps/klipper/ecrans" "ui" "ui/widgets"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_http_client esp_websocket_client json app_update esp_timer esp_partition PandaTouch_IDF lvgl
)
