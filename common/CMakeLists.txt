add_library(RuffnecKkSuiteBuildPolicy INTERFACE)
add_library(RuffnecKk::BuildPolicy ALIAS RuffnecKkSuiteBuildPolicy)

target_compile_features(RuffnecKkSuiteBuildPolicy INTERFACE cxx_std_20)
target_compile_definitions(
    RuffnecKkSuiteBuildPolicy
    INTERFACE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(
        RuffnecKkSuiteBuildPolicy
        INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
    )

    if(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS)
        target_compile_options(
            RuffnecKkSuiteBuildPolicy INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:/WX>
        )
    endif()

    # Make release artifacts byte-for-byte reproducible across clean build
    # directories. Without /Brepro, LINK stamps the PE and debug directory
    # with the wall-clock build time, which breaks component hashes even when
    # every source and compiler input is identical.
    target_link_options(RuffnecKkSuiteBuildPolicy INTERFACE /Brepro)
else()
    target_compile_options(
        RuffnecKkSuiteBuildPolicy
        INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
    )

    if(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS)
        target_compile_options(
            RuffnecKkSuiteBuildPolicy INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-Werror>
        )
    endif()
endif()

add_library(RuffnecKkSuiteCommon INTERFACE)
add_library(RuffnecKk::Common ALIAS RuffnecKkSuiteCommon)
target_include_directories(RuffnecKkSuiteCommon INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
    RuffnecKkSuiteCommon
    INTERFACE
        RuffnecKk::BuildPolicy
        D2RLPlugin::D2RLPlugin
)

add_library(RuffnecKkSuiteCommonV4 INTERFACE)
add_library(RuffnecKk::CommonV4 ALIAS RuffnecKkSuiteCommonV4)
target_include_directories(RuffnecKkSuiteCommonV4 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
    RuffnecKkSuiteCommonV4
    INTERFACE
        RuffnecKk::BuildPolicy
        D2RLPluginV4::D2RLPluginV4
)

add_library(RuffnecKkSuiteNativeStatCompat STATIC src/native_stat_compat.cpp)
add_library(RuffnecKk::NativeStatCompat ALIAS RuffnecKkSuiteNativeStatCompat)
target_include_directories(RuffnecKkSuiteNativeStatCompat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(RuffnecKkSuiteNativeStatCompat PUBLIC RuffnecKk::BuildPolicy)

if(BUILD_TESTING)
    add_executable(ruffneckk_tracked_native_transform_tests tests/tracked_native_transform_tests.cpp)
    target_link_libraries(ruffneckk_tracked_native_transform_tests PRIVATE RuffnecKk::BuildPolicy)
    target_include_directories(ruffneckk_tracked_native_transform_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME ruffneckk.common.tracked-native-transform COMMAND ruffneckk_tracked_native_transform_tests)

    add_executable(ruffneckk_native_stat_compat_tests tests/native_stat_compat_tests.cpp)
    target_link_libraries(ruffneckk_native_stat_compat_tests PRIVATE RuffnecKk::NativeStatCompat)
    add_test(NAME ruffneckk.common.native-stat-compat COMMAND ruffneckk_native_stat_compat_tests)

    add_executable(ruffneckk_localization_service_tests tests/localization_tests.cpp)
    target_link_libraries(ruffneckk_localization_service_tests PRIVATE RuffnecKk::Common)
    add_test(NAME ruffneckk.common.localization COMMAND ruffneckk_localization_service_tests)
endif()
