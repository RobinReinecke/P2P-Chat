cmake_minimum_required(VERSION 3.16)
project(aufgabe_4)

# add dependencies
include(cmake/CPM.cmake)

set(CMAKE_CXX_STANDARD 14)

CPMAddPackage(
        NAME nlohmann_json
        VERSION 3.6.1
        # the git repo is incredibly large, so we download the archived include directory
        URL https://github.com/nlohmann/json/releases/download/v3.6.1/include.zip
        URL_HASH SHA256=69cc88207ce91347ea530b227ff0776db82dcb8de6704e1a3d74f4841bc651cf
)

if (nlohmann_json_ADDED)
    add_library(nlohmann_json INTERFACE IMPORTED)
    target_include_directories(nlohmann_json INTERFACE ${nlohmann_json_SOURCE_DIR})
endif()

CPMAddPackage(
        NAME cxxopts
        GITHUB_REPOSITORY jarro2783/cxxopts
        VERSION 2.2.1
        OPTIONS
        "CXXOPTS_BUILD_EXAMPLES Off"
        "CXXOPTS_BUILD_TESTS Off"
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB src "src/*.cpp")

find_package(Threads REQUIRED)

add_library(clientLib STATIC ${src})
target_link_libraries(clientLib ${CMAKE_THREAD_LIBS_INIT} nlohmann_json gvc cgraph crypto)

add_executable(client main.cpp)
target_link_libraries(client clientLib cxxopts)