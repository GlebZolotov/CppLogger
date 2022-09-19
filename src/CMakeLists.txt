set(SOURCE_EXE CppLogger.cpp)

find_package(Threads REQUIRED)
find_package(Boost COMPONENTS thread chrono REQUIRED)

add_library(CppLogger ${SOURCE_EXE})	# Создает исполняемый файл с именем main
target_include_directories(CppLogger PUBLIC
    ./
    ${Boost_INCLUDE_DIRS}
)
target_link_libraries(CppLogger PRIVATE 
    Threads::Threads 
    Boost::thread 
    Boost::chrono 
    stdc++fs
)

# install actions
file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
install(FILES ${files} DESTINATION include/CppLogger)

install(TARGETS CppLogger DESTINATION lib)