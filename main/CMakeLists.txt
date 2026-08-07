idf_component_register(
    SRCS "main.cpp"
         "display/display.cpp"
         "touch/touch.cpp"
         "ui/lvgl_port.cpp"
         "ui/ui_hello.cpp"
         "ui/ui_calib.cpp"
         "scan_engine/scan_engine.cpp"
    INCLUDE_DIRS "include" "display" "touch" "ui" "scan_engine"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_uart esp_driver_i2c esp_lcd esp_timer esp_event esp_netif esp_wifi nvs_flash lvgl__lvgl
)
