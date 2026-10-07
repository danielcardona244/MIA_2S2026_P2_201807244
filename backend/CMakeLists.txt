cmake_minimum_required(VERSION 3.10)
project(extreamfs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

file(GLOB SOURCES "src/*.cpp")
add_executable(extreamfs ${SOURCES})

# Utilidades locales de pruebas. Son opcionales para que el proyecto siga
# compilando aunque backend/tests/ se mantenga fuera del repositorio.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_structs.cpp")
    add_executable(test_structs tests/test_structs.cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sprint2.cpp")
    add_executable(test_sprint2 tests/test_sprint2.cpp)
endif()
