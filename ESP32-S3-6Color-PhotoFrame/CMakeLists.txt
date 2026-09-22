# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "2.1.0")
# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)
set(CMAKE_NINJA_FORCE_RESPONSE_FILE 1 CACHE BOOL "Force response files" FORCE)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

# Auto-generate photo_web_pages.h from dashboard.html at build time
# Edit dashboard.html directly — no more C++ raw-string pain
#
# 注意：这里必须用 add_custom_command(OUTPUT ...) 而不是 add_custom_target。
# 用 add_custom_target 时 ninja 会在“同一次构建”里先算好依赖、后生成文件，
# 于是认为 photo_web_server.cpp 仍是最新的 → 改了 dashboard.html 却不重编，
# 静默产出不含新 UI 的固件（已踩过一次）。声明 OUTPUT 后，header 一旦被重新
# 生成，依赖它的 .cpp 就会被重编；且 CMake 默认带 restat，脚本内容没变时不
# 会触发多余的重新编译。
set(DASHBOARD_HTML "${CMAKE_CURRENT_SOURCE_DIR}/components/user_app_bsp/mode_src/dashboard.html")
set(WEB_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/components/user_app_bsp/mode_src/photo_web_pages.h")
add_custom_command(
    OUTPUT "${WEB_HEADER}"
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/components/user_app_bsp/mode_src/html_to_header.py"
            "${DASHBOARD_HTML}" "${WEB_HEADER}"
    DEPENDS "${DASHBOARD_HTML}"
    COMMENT "Generating photo_web_pages.h from dashboard.html"
    VERBATIM
)
add_custom_target(gen_web_pages ALL DEPENDS "${WEB_HEADER}")

