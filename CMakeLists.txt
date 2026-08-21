cmake_minimum_required(VERSION 3.20)

project(obs-tw-adsnooze VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(BUILD_OBS_PLUGIN "Build the native OBS plugin module" OFF)

add_library(
  adsnooze-core STATIC
  src/core/audio-activity-detector.cpp
  src/core/chat-activity-tracker.cpp
  src/core/rfc3339.cpp
  src/core/snooze-policy.cpp
)

target_include_directories(adsnooze-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(adsnooze-core PUBLIC cxx_std_20)
set_target_properties(adsnooze-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(adsnooze-core PRIVATE /W4 /permissive-)
else()
  target_compile_options(adsnooze-core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(BUILD_TESTING)
  add_executable(adsnooze-core-tests tests/core-tests.cpp src/obs/audio-monitor-registry.cpp)
  target_link_libraries(adsnooze-core-tests PRIVATE adsnooze-core)
  target_compile_features(adsnooze-core-tests PRIVATE cxx_std_20)
  add_test(NAME adsnooze-core-tests COMMAND adsnooze-core-tests)
endif()

if(BUILD_OBS_PLUGIN)
  find_package(libobs REQUIRED)
  find_package(CURL REQUIRED)

  add_library(
    ${PROJECT_NAME} MODULE
    src/obs/audio-monitor-filter.cpp
    src/obs/audio-monitor-registry.cpp
    src/obs/plugin-config.cpp
    src/obs/plugin-main.cpp
    src/obs/plugin-runtime.cpp
    src/twitch/twitch-ad-client.cpp
  )

  target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
  target_link_libraries(${PROJECT_NAME} PRIVATE adsnooze-core OBS::libobs CURL::libcurl)
  target_compile_definitions(${PROJECT_NAME} PRIVATE PLUGIN_VERSION="${PROJECT_VERSION}")
  set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

  if(COMMAND set_target_properties_plugin)
    set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
  endif()

  install(
    TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "obs-plugins/64bit"
    LIBRARY DESTINATION "obs-plugins/64bit"
  )
  install(DIRECTORY data/ DESTINATION "data/obs-plugins/${PROJECT_NAME}")
endif()
