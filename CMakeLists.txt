cmake_minimum_required (VERSION 3.8)
project (CppLogger)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_FLAGS "-Wall -g")
SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -funroll-loops") #-finline-functions
SET(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g -Wall -Wextra -Wpedantic")

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)

set(SOURCE_EXE test.cpp)

set(CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/deps ${CMAKE_PREFIX_PATH})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Boost COMPONENTS thread chrono REQUIRED)

INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})

add_executable(test ${SOURCE_EXE})	# Создает исполняемый файл с именем main
target_link_libraries(test PRIVATE Threads::Threads ${CMAKE_SOURCE_DIR}/deps/lib/libboost_thread.a ${CMAKE_SOURCE_DIR}/deps/lib/libboost_chrono.a stdc++fs)

## src headers
file(GLOB_RECURSE HEADERS_FOR_INSTALL
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
foreach (file ${HEADERS_FOR_INSTALL})
    get_filename_component(dir ${file} DIRECTORY)
    install(FILES ${file} DESTINATION include/${CMAKE_PROJECT_NAME})
endforeach()