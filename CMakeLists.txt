cmake_minimum_required(VERSION 3.20)
project(StarfieldHeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include cameraunlock-core shared library
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# MinHook sources
set(MINHOOK_SOURCES
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)

# inih source
set(INIH_SOURCES
    extern/ini.c
)

# Mod source files
set(MOD_SOURCES
    src/pch.cpp
    src/dllmain.cpp
    src/core/mod.cpp
    src/core/logger.cpp
    src/core/config.cpp
    src/legacy_config/legacy_config.cpp
    src/core/path_utils.cpp
    src/core/rtti_utils.cpp
    src/hooks/hook_manager.cpp
    src/hooks/camera_hook.cpp
    src/hooks/aim_hook.cpp
    src/hooks/weapon_hook.cpp
    src/game/build_profile.cpp
    src/game/gdk_offsets.cpp
    src/game/steam_offsets.cpp
    src/game/base_fov.cpp
    src/game/helmet_light.cpp
    src/hooks/input_hook.cpp
    src/game/game_state.cpp
    src/game/scene_layout.cpp
    src/ui/stock_reticle.cpp
)

# Create ASI (DLL)
add_library(StarfieldHeadTracking SHARED
    ${MINHOOK_SOURCES}
    ${INIH_SOURCES}
    ${MOD_SOURCES}
)

# Exclude C sources from precompiled headers (pch.h contains C++ headers)
set_source_files_properties(${MINHOOK_SOURCES} ${INIH_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Precompiled header
target_precompile_headers(StarfieldHeadTracking PRIVATE src/pch.h)

# Output as .asi for Ultimate ASI Loader
set_target_properties(StarfieldHeadTracking PROPERTIES
    OUTPUT_NAME "StarfieldHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Link cameraunlock core library
target_link_libraries(StarfieldHeadTracking PRIVATE cameraunlock)

option(STARFIELDHT_DEV_HOTKEYS "Build the diagnostic hotkeys (developer aid, not for release)" OFF)
if(STARFIELDHT_DEV_HOTKEYS)
    target_sources(StarfieldHeadTracking PRIVATE src/core/access_probe.cpp src/core/object_finder.cpp src/game/hud_probe.cpp)
    target_compile_definitions(StarfieldHeadTracking PRIVATE STARFIELDHT_DEV_HOTKEYS=1)
    message(WARNING "STARFIELDHT_DEV_HOTKEYS is ON. This build registers F8 to cycle "
                    "rotation-axis isolation and Insert to dump the camera matrices "
                    "to the log. Do not ship it.")
endif()

# Include directories
target_include_directories(StarfieldHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

# Windows libraries. psapi is here because cameraunlock's crash handler and
# pattern scanner call EnumProcessModules and GetModuleInformation and core does
# not link it itself. ws2_32 arrives transitively from cameraunlock; the D3D,
# shell and version libraries were left over from an overlay path this mod does
# not have.
if(WIN32)
    target_link_libraries(StarfieldHeadTracking PRIVATE psapi)
endif()

# Behaviour locks for the camera boundary and the render-side maths, all pure
# functions of their inputs. Separate executables linking none of the hook or
# Windows machinery, so `pixi run test` runs anywhere the mod builds, with no
# game installed.
option(STARFIELDHT_BUILD_TESTS "Build the camera behaviour tests" ON)
if(STARFIELDHT_BUILD_TESTS)
    enable_testing()
    add_executable(starfieldht_lean_tests tests/lean_direction_tests.cpp)
    target_include_directories(starfieldht_lean_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include
    )
    add_test(NAME lean_direction COMMAND starfieldht_lean_tests)
    add_executable(starfieldht_weapon_tests tests/weapon_projection_tests.cpp)
    target_include_directories(starfieldht_weapon_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME weapon_projection COMMAND starfieldht_weapon_tests)
    add_executable(starfieldht_aim_tests tests/aim_projection_tests.cpp)
    target_include_directories(starfieldht_aim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME aim_projection COMMAND starfieldht_aim_tests)
    add_executable(starfieldht_impact_tests tests/impact_projection_tests.cpp)
    target_include_directories(starfieldht_impact_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME impact_projection COMMAND starfieldht_impact_tests)
    add_executable(starfieldht_camera_frame_tests tests/camera_frame_tests.cpp)
    target_include_directories(starfieldht_camera_frame_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME camera_frame_history COMMAND starfieldht_camera_frame_tests)
    add_executable(starfieldht_head_rotation_tests tests/head_rotation_tests.cpp)
    target_include_directories(starfieldht_head_rotation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME head_rotation COMMAND starfieldht_head_rotation_tests)
    add_executable(starfieldht_head_attached_tests tests/head_attached_tests.cpp)
    target_include_directories(starfieldht_head_attached_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME head_attached COMMAND starfieldht_head_attached_tests)

    # The committed HeadTracking.ini against the table, and the toggles' saves.
    # `--render-config <path>` makes it write the committed file instead
    # (pixi run render-config).
    add_executable(starfieldht_config_tests
        tests/config_tests.cpp
        src/core/config.cpp
        src/legacy_config/legacy_config.cpp
        src/core/logger.cpp
        src/core/path_utils.cpp
        extern/ini.c
    )
    target_include_directories(starfieldht_config_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
    )
    target_compile_definitions(starfieldht_config_tests PRIVATE
        SF_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")
    target_link_libraries(starfieldht_config_tests PRIVATE cameraunlock)
    add_test(NAME config_tests COMMAND starfieldht_config_tests)

    # The config differential test. The oracle is the dev pre-release's reader (d276538), the
    # newest published build, copied byte for byte into tests/config_differential/oracle with
    # the logger and path helpers it links; renaming its namespace lets it link beside this
    # build's. oracle_adapter.cpp copies its Config out for the test. inih is compiled once,
    # into the test: extern/ini.c has not changed since that build.
    set(SF_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle)
    add_library(sf_config_oracle STATIC
        ${SF_ORACLE_DIR}/src/core/config.cpp
        ${SF_ORACLE_DIR}/src/core/logger.cpp
        ${SF_ORACLE_DIR}/src/core/path_utils.cpp
        tests/config_differential/oracle_adapter.cpp
    )
    target_include_directories(sf_config_oracle PRIVATE
        ${SF_ORACLE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(sf_config_oracle PRIVATE StarfieldHT=sf_oracle)
    target_link_libraries(sf_config_oracle PRIVATE cameraunlock)

    add_executable(starfieldht_config_differential
        tests/config_differential/differential_tests.cpp
        src/core/config.cpp
        src/legacy_config/legacy_config.cpp
        src/core/logger.cpp
        src/core/path_utils.cpp
        extern/ini.c
    )
    target_include_directories(starfieldht_config_differential PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(starfieldht_config_differential PRIVATE
        SF_DIFFERENTIAL_INPUTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/inputs"
        SF_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")
    target_link_libraries(starfieldht_config_differential PRIVATE cameraunlock sf_config_oracle)
    add_test(NAME config_differential COMMAND starfieldht_config_differential)

    # Core's canonical config lint over the committed file and every file the differential
    # test migrated, which it writes beside itself under migrated\.
    add_test(NAME config_lint COMMAND node
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/lint-migrated.mjs
        $<TARGET_FILE_DIR:starfieldht_config_differential>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential)

    # The frozen reader, the core sources it compiles, the oracle and the inputs against the
    # hashes tests/config_differential/frozen.tsv records.
    add_test(NAME config_frozen COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/check-frozen.ps1)
endif()
