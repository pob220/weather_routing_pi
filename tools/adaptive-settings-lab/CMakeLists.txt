cmake_minimum_required(VERSION 3.20)
project(adaptive_settings_lab LANGUAGES CXX)
# Standalone qualification only: none of this target enters the plugin.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ATLAS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEATHER_ROUTING_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEATHER_ROUTING_ENGINE_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ORIGINAL_ROUTING_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_subdirectory("${ROOT}/vendor/atlas-routing-engine" atlas)
add_subdirectory("${ROOT}/vendor/original-routing-engine" original)
add_library(adaptive_lab_quick2 STATIC "${ROOT}/vendor/quick2-routing-engine/src/Engine.cpp")
target_include_directories(adaptive_lab_quick2 PUBLIC "${ROOT}/vendor/quick2-routing-engine/include"
  PRIVATE "${ROOT}/vendor/weather-routing-engine/src")
target_link_libraries(adaptive_lab_quick2 PUBLIC weather_routing_engine)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
pkg_check_modules(ECCODES REQUIRED IMPORTED_TARGET eccodes)
find_package(wxWidgets REQUIRED COMPONENTS core base net xml html adv aui)
include(${wxWidgets_USE_FILE})
add_executable(adaptive-lab Runner.cpp
  "${ROOT}/src/ChartSafetyCache.cpp" "${ROOT}/src/ChartHazardEvaluator.cpp"
  "${ROOT}/src/AppendOnlyCache.cpp" "${ROOT}/src/SystemMemory.cpp")
target_include_directories(adaptive-lab PRIVATE support
  "${ROOT}/vendor/atlas-routing-engine/tests" "${ROOT}/include"
  "${ROOT}/opencpn-libs/api-21")
target_compile_definitions(adaptive-lab PRIVATE ATLAS_HAS_ECCODES=1 LAB_HAS_CHARTS=1
  LAB_VARIANT="current-frozen" WEATHER_ROUTING_DECLARE_OPTIONAL_CHART_SAFETY_ABI=1)
target_link_libraries(adaptive-lab PRIVATE atlas_routing_engine adaptive_lab_quick2
  hardened_original_engine quick_weather_routing_engine alternative_weather_routing_engine
  PkgConfig::JSONCPP PkgConfig::ECCODES ${wxWidgets_LIBRARIES})
