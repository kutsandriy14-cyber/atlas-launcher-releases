cmake_minimum_required(VERSION 3.16)

project(AtlasLauncher
    VERSION 0.3.2
    DESCRIPTION "Atlas Launcher - Minecraft Java launcher for Windows 7+"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Network)

set(ATLAS_SOURCES
    src/main.cpp
    src/domain/types.h
    src/infrastructure/logger.h
    src/infrastructure/logger.cpp
    src/infrastructure/json_store.h
    src/infrastructure/json_store.cpp
    src/services/instance_service.h
    src/services/instance_service.cpp
    src/services/settings_service.h
    src/services/settings_service.cpp
    src/services/package_service.h
    src/services/package_service.cpp
    src/services/download_manager.h
    src/services/download_manager.cpp
    src/services/update_service.h
    src/services/update_service.cpp
    src/services/java_runtime_service.h
    src/services/java_runtime_service.cpp
    src/services/minecraft_install_service.h
    src/services/minecraft_install_service.cpp
    src/services/loader_install_service.h
    src/services/loader_install_service.cpp
    src/services/auth_service.h
    src/services/auth_service.cpp
    src/services/launch_service.h
    src/services/launch_service.cpp
    src/providers/modrinth_client.h
    src/providers/modrinth_client.cpp
    src/providers/catalog_project.h
    src/providers/curseforge_client.h
    src/providers/curseforge_client.cpp
    src/ui/main_window.h
    src/ui/main_window.cpp
    src/ui/atlas_theme.h
    src/ui/atlas_theme.cpp
    assets/resources.qrc
)

add_executable(AtlasLauncher WIN32 ${ATLAS_SOURCES})

add_executable(AtlasUpdater WIN32
    src/updater/main.cpp
    src/services/update_service.h
    src/services/update_service.cpp
)

if(CMAKE_CROSSCOMPILING)
    # Qt's target package points at Windows tools.  CMake must instead use
    # native Qt utilities to generate C++ sources while cross-compiling.
    find_program(ATLAS_HOST_MOC NAMES moc HINTS /usr/lib/qt5/bin)
    find_program(ATLAS_HOST_RCC NAMES rcc HINTS /usr/lib/qt5/bin)
    find_program(ATLAS_HOST_UIC NAMES uic HINTS /usr/lib/qt5/bin)
    if(NOT ATLAS_HOST_MOC OR NOT ATLAS_HOST_RCC OR NOT ATLAS_HOST_UIC)
        message(FATAL_ERROR "Native Qt MOC/RCC/UIC tools are required for cross-compilation")
    endif()
    set_property(TARGET AtlasLauncher PROPERTY AUTOMOC_EXECUTABLE "${ATLAS_HOST_MOC}")
    set_property(TARGET AtlasLauncher PROPERTY AUTORCC_EXECUTABLE "${ATLAS_HOST_RCC}")
    set_property(TARGET AtlasLauncher PROPERTY AUTOUIC_EXECUTABLE "${ATLAS_HOST_UIC}")
    set_property(TARGET AtlasUpdater PROPERTY AUTOMOC_EXECUTABLE "${ATLAS_HOST_MOC}")
    set_property(TARGET AtlasUpdater PROPERTY AUTORCC_EXECUTABLE "${ATLAS_HOST_RCC}")
    set_property(TARGET AtlasUpdater PROPERTY AUTOUIC_EXECUTABLE "${ATLAS_HOST_UIC}")
endif()

target_include_directories(AtlasLauncher PRIVATE src)
target_include_directories(AtlasUpdater PRIVATE src)

target_link_libraries(AtlasLauncher PRIVATE
    Qt5::Widgets
    Qt5::Network
)
target_link_libraries(AtlasUpdater PRIVATE
    Qt5::Widgets
    Qt5::Network
)

if (WIN32)
    target_link_libraries(AtlasLauncher PRIVATE crypt32)
    target_link_libraries(AtlasUpdater PRIVATE crypt32)
endif()

# Small console diagnostic shipped only for release verification. It reports
# whether Qt can load the dynamic OpenSSL backend beside the executable.
add_executable(TlsRuntimeProbe tests/tls_runtime_probe.cpp)
set_target_properties(TlsRuntimeProbe PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)
target_link_libraries(TlsRuntimeProbe PRIVATE Qt5::Core Qt5::Network)
if (WIN32)
    target_link_libraries(TlsRuntimeProbe PRIVATE crypt32)
endif()

# Headless probe: exercises the full Vanilla install pipeline (Mojang manifest,
# version metadata, client JAR, libraries and asset index) without a GUI.
# It is deliberately native-only: a Windows moc.exe cannot be executed in Linux
# during the MinGW cross-build of the distributable launcher.
if (NOT CMAKE_CROSSCOMPILING)
    add_executable(VanillaPipelineProbe
        tests/vanilla_pipeline_test.cpp
        src/services/download_manager.cpp
        src/services/minecraft_install_service.cpp
        src/infrastructure/logger.cpp
    )
    set_target_properties(VanillaPipelineProbe PROPERTIES AUTORCC OFF AUTOUIC OFF)
    target_include_directories(VanillaPipelineProbe PRIVATE src)
    target_link_libraries(VanillaPipelineProbe PRIVATE Qt5::Core Qt5::Network)

    # Confirms that each loader selector receives a live, compatible list from
    # its official metadata source before the UI exposes it to the user.
    add_executable(LoaderVersionProbe
        tests/loader_version_probe.cpp
        src/services/download_manager.cpp
        src/services/loader_install_service.cpp
        src/infrastructure/logger.cpp
    )
    set_target_properties(LoaderVersionProbe PROPERTIES AUTORCC OFF AUTOUIC OFF)
    target_include_directories(LoaderVersionProbe PRIVATE src)
    target_link_libraries(LoaderVersionProbe PRIVATE Qt5::Core Qt5::Network)

    # Regression probe for the in-dialog manual refresh path. A cancelled
    # manifest reply must never consume the newer active Mojang request.
    add_executable(MinecraftVersionsRefreshProbe
        tests/minecraft_versions_refresh_probe.cpp
        src/services/download_manager.cpp
        src/services/minecraft_install_service.cpp
        src/infrastructure/logger.cpp
    )
    set_target_properties(MinecraftVersionsRefreshProbe PROPERTIES AUTORCC OFF AUTOUIC OFF)
    target_include_directories(MinecraftVersionsRefreshProbe PRIVATE src)
    target_link_libraries(MinecraftVersionsRefreshProbe PRIVATE Qt5::Core Qt5::Network)
endif()

if (MSVC)
    target_compile_options(AtlasLauncher PRIVATE /W4 /permissive-)
else()
    target_compile_options(AtlasLauncher PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (WIN32)
    set_target_properties(AtlasLauncher PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "AtlasLauncher"
    )
    set_target_properties(AtlasUpdater PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "AtlasUpdater"
    )
endif()

install(TARGETS AtlasLauncher AtlasUpdater RUNTIME DESTINATION .)
install(DIRECTORY tools DESTINATION . FILES_MATCHING PATTERN "7za.exe" PATTERN "7za.dll" PATTERN "7zip-LICENSE.txt" PATTERN "7zip-SHA256.txt")
install(DIRECTORY examples DESTINATION .)
install(DIRECTORY docs DESTINATION .)

include(CTest)
option(ATLAS_ENABLE_NETWORK_INTEGRATION_TESTS
       "Run slow network integration tests that download Minecraft files" OFF)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(PackageServiceTests
        tests/package_service_test.cpp
        src/services/package_service.cpp
        src/infrastructure/json_store.cpp
    )
    target_include_directories(PackageServiceTests PRIVATE src)
    target_link_libraries(PackageServiceTests PRIVATE Qt5::Core)
    add_test(NAME PackageServiceTests COMMAND PackageServiceTests)

    add_executable(OfflineNicknameTests
        tests/offline_nickname_test.cpp
        src/services/settings_service.cpp
        src/services/auth_service.cpp
        src/infrastructure/json_store.cpp
        src/infrastructure/logger.cpp
    )
    target_include_directories(OfflineNicknameTests PRIVATE src)
    target_link_libraries(OfflineNicknameTests PRIVATE Qt5::Core Qt5::Network)
    if(WIN32)
        target_link_libraries(OfflineNicknameTests PRIVATE crypt32)
    endif()
    add_test(NAME OfflineNicknameTests COMMAND OfflineNicknameTests)
    # VanillaPipelineProbe requires a writable target directory and performs a
    # large live download. Keep it opt-in, rather than registering a test that
    # always fails without its required command-line arguments.
    if(ATLAS_ENABLE_NETWORK_INTEGRATION_TESTS)
        add_test(NAME VanillaPipelineProbe
                 COMMAND VanillaPipelineProbe
                         "${CMAKE_BINARY_DIR}/ctest-vanilla-instance" "1.12.2")
    endif()
    add_test(NAME LoaderVersionProbe COMMAND LoaderVersionProbe)
    add_test(NAME MinecraftVersionsRefreshProbe COMMAND MinecraftVersionsRefreshProbe)
endif()
