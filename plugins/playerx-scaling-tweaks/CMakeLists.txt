if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(PlayerXScalingTweaks VERSION 1.0.1 LANGUAGES CXX RC)
    include(CTest)
    option(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
else()
    enable_language(RC)
endif()

include(FetchContent)
if(NOT TARGET tomlplusplus::tomlplusplus)
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

file(READ
    "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-playerx-scaling-tweaks.toml"
    PLAYER_SCALING_DEFAULT_TOML)
configure_file(
    src/default_config.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/default_config.hpp"
    @ONLY
    NEWLINE_STYLE LF)

add_library(ruffneckk_playerx_scaling_tweaks SHARED
    src/plugin.cpp
    src/plugin.rc
)
target_compile_features(ruffneckk_playerx_scaling_tweaks PRIVATE cxx_std_20)
target_compile_definitions(ruffneckk_playerx_scaling_tweaks PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_include_directories(ruffneckk_playerx_scaling_tweaks PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(ruffneckk_playerx_scaling_tweaks PRIVATE
    D2RLPlugin::D2RLPlugin
    RuffnecKk::NativeStatCompat
    tomlplusplus::tomlplusplus
)
set_target_properties(ruffneckk_playerx_scaling_tweaks PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-playerx-scaling-tweaks"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
d2rlplugin_embed_config(
    TARGET ruffneckk_playerx_scaling_tweaks
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-playerx-scaling-tweaks.toml"
)

if(MSVC)
    target_compile_options(ruffneckk_playerx_scaling_tweaks PRIVATE
        /utf-8 /W4 /permissive- /EHsc
        $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_link_options(ruffneckk_playerx_scaling_tweaks PRIVATE
        /INCREMENTAL:NO /Brepro
    )
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_playerx_scaling_tweaks_tests
        tests/policy_tests.cpp
    )
    target_compile_features(ruffneckk_playerx_scaling_tweaks_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_playerx_scaling_tweaks_tests PRIVATE
        D2RLPlugin::D2RLPlugin
        tomlplusplus::tomlplusplus
    )
    target_include_directories(ruffneckk_playerx_scaling_tweaks_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(ruffneckk_playerx_scaling_tweaks_tests PRIVATE
        PLAYER_SCALING_PLUGIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
        PLAYER_SCALING_TOML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-playerx-scaling-tweaks.toml"
        PLAYER_SCALING_FINGERPRINT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/native-fingerprint.json"
    )
    if(MSVC)
        target_compile_options(ruffneckk_playerx_scaling_tweaks_tests PRIVATE
            /utf-8 /W4 /permissive- /EHsc
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
        )
    endif()
    add_test(
        NAME ruffneckk.playerx-scaling-tweaks.policy
        COMMAND ruffneckk_playerx_scaling_tweaks_tests
    )
endif()
