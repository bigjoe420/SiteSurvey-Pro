# =============================================================================
# SiteSurvey Pro — Root CMakeLists.txt
# ESP-IDF v6.1 project for Guition NM-CYD-C5 (ESP32-C5 RISC-V)
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# Set the target before including project.cmake
set(IDF_TARGET esp32c5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SiteSurvey-Pro)
