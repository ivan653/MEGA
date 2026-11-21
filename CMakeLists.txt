cmake_minimum_required(VERSION 3.20)
project(MEGA VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

add_executable(mega
    src/main.cpp
    src/render/Shader.cpp
    src/render/Framebuffer.cpp
    src/render/TextureAtlas.cpp
    src/render/WorldMesher.cpp
    src/render/BillboardRenderer.cpp
    src/render/RobotRenderer.cpp
    src/render/HudRenderer.cpp
    src/world/WorldGrid.cpp
    src/world/LightBaker.cpp
    src/world/gen/MegaGenerator.cpp
    src/game/Player.cpp
    src/game/GameWorld.cpp
    src/audio/AudioEngine.cpp
)

target_include_directories(mega PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mega PRIVATE
    SDL3::SDL3
    OpenGL::GL
    PkgConfig::EPOXY
)

target_compile_options(mega PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-g -O0>
)

# Optional GoogleTest for generator & collision unit tests
find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()
    add_executable(mega_tests
        tests/CoreTests.cpp
        src/world/WorldGrid.cpp
        src/world/LightBaker.cpp
        src/world/gen/MegaGenerator.cpp
        src/game/Player.cpp
        src/game/GameWorld.cpp
        src/audio/AudioEngine.cpp
    )
    target_include_directories(mega_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mega_tests PRIVATE GTest::gtest_main SDL3::SDL3)
    include(GoogleTest)
    gtest_discover_tests(mega_tests)
endif()
