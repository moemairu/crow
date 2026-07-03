cmake_minimum_required(VERSION 3.20)
project(crow VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

# --- Sources ---
file(GLOB CROW_SOURCES src/*.c)

# --- Executable ---
add_executable(crow
    src/main.c
    src/config.c
    src/crow_mod.c
    src/crow_pak.c
    src/crow_window.c
)

target_include_directories(crow PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(crow PRIVATE PkgConfig::GTK4)

# --- Compiler Warnings ---
target_compile_options(crow PRIVATE -Wall -Wextra -Wpedantic)
