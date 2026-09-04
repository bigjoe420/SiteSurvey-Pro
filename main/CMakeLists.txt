idf_component_register(
    SRCS "main.cpp"
         "display/display.cpp"
         "touch/touch.cpp"
         "ui/lvgl_port.cpp"
         "ui/ui_home.cpp"
         "ui/ui_wifi.cpp"
         "ui/ui_wifi_detail.cpp"
         "ui/ui_ble.cpp"
         "ui/ui_env.cpp"
         "ui/ui_spectrum.cpp"
         "ui/ui_gps.cpp"
         "ui/ui_alerts.cpp"
         "ui/ui_settings.cpp"
         "ui/ui_spin3d.cpp"
         "ui/ui_calib.cpp"
         "ui/ui_splash.cpp"
         "scan_engine/scan_engine.cpp"
         "ble/scan_ble.cpp"
         "sensors/bme680.cpp"
         "sensors/gps.cpp"
         "sensors/sensors.cpp"
         "storage/sd_card.cpp"
         "storage/session_logger.cpp"
         "alerts/alert_engine.cpp"
    INCLUDE_DIRS "include" "display" "touch" "ui" "scan_engine" "ble" "sensors" "storage" "alerts"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_uart esp_driver_i2c esp_driver_sdspi esp_lcd esp_timer esp_event esp_netif esp_wifi nvs_flash fatfs sdmmc bt lvgl__lvgl
)

# Panic instead of infinite loop on LVGL assert (fixes WDT hang)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_ASSERT_HANDLER="assert(0);")
