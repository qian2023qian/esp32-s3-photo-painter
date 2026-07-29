# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "2.0.1")
# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)
set(CMAKE_NINJA_FORCE_RESPONSE_FILE 1 CACHE BOOL "Force response files" FORCE)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

# Auto-generate photo_web_pages.h from dashboard.html at build time
# Edit dashboard.html directly — no more C++ raw-string pain
set(DASHBOARD_HTML "${CMAKE_CURRENT_SOURCE_DIR}/components/user_app_bsp/mode_src/dashboard.html")
set(WEB_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/components/user_app_bsp/mode_src/photo_web_pages.h")
add_custom_target(gen_web_pages ALL
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/components/user_app_bsp/mode_src/html_to_header.py"
            "${DASHBOARD_HTML}" "${WEB_HEADER}"
    DEPENDS "${DASHBOARD_HTML}"
    COMMENT "Generating photo_web_pages.h from dashboard.html"
    VERBATIM
)

