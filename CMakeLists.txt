# ==============================================================================
# 1v1 Neon Space Arena - Cross-Platform CMake Build Configuration
# Shared Build File: Developer A (Graphics/Shaders) & Developer B (Net/Gameplay)
# ==============================================================================
cmake_minimum_required(VERSION 3.16)
project(NeonSpaceArena VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure compatibility when third-party dependencies specify older CMake versions
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for dependencies" FORCE)

include(FetchContent)

# ------------------------------------------------------------------------------
# 1. Fetch Raylib (Hardware-accelerated OpenGL 3.3 rendering & GLSL shaders)
# ------------------------------------------------------------------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)

# ------------------------------------------------------------------------------
# 2. Fetch ENet (Reliable & unreliable UDP networking library)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG        v1.3.18
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(raylib enet)

# ------------------------------------------------------------------------------
# 3. Executable Target
# ------------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/network/NetworkManager.cpp
    src/game/Simulation.cpp
    src/graphics/Renderer.cpp
)

set(HEADERS
    src/shared/Constants.hpp
    src/shared/Protocol.hpp
    src/network/NetworkManager.hpp
    src/game/Simulation.hpp
    src/graphics/Renderer.hpp
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${enet_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    raylib
    enet
)

# Platform-specific system libraries required by ENet and Raylib
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 winmm)
    if(MINGW)
        # Link C/C++ runtime statically on MinGW so the .exe runs standalone on any PC
        target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread dl)
endif()

# ------------------------------------------------------------------------------
# 4. Copy assets/ directory to build output directories
# ------------------------------------------------------------------------------
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    COMMENT "Copying assets/ to executable output directory..."
)
