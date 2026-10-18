cmake_minimum_required(VERSION 3.16)
project(preset_name_lvgl LANGUAGES C CXX)
get_filename_component(ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(LV_BUILD_CONF_PATH "${ROOT}/include/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/.pio/libdeps/panelan-lvgl-controller/lvgl" lvgl)
add_executable(preset_name_lvgl main.cpp)
target_include_directories(preset_name_lvgl PRIVATE "${ROOT}/src")
target_link_libraries(preset_name_lvgl PRIVATE lvgl m)
# Exercise the production position update, not a second implementation of it.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ROOT}/src/PanelLanLVGLUI.cpp")
file(READ "${ROOT}/src/PanelLanLVGLUI.cpp" UI)
string(REGEX MATCH "const int32_t nameY =[^}]+}" POSITION_CODE "${UI}")
if(NOT POSITION_CODE)
    message(FATAL_ERROR "Production name position block not found")
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/position.h" CONTENT
"inline bool applyPosition(lv_obj_t *presetNameLabel_, const PresetNamePresentation::Layout &fittedName) {
    auto *hero = lv_obj_get_parent(presetNameLabel_);
    bool presetCardDirty = false;
    ${POSITION_CODE}
    return presetCardDirty;
}")
target_include_directories(preset_name_lvgl PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(preset_name_lvgl PRIVATE -UNDEBUG)
enable_testing()
add_test(NAME preset_name_lvgl COMMAND preset_name_lvgl)
