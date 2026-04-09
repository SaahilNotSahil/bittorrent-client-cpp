cmake_minimum_required(VERSION 3.13)

project(bittorrent-client-cpp)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SOURCE_FILES src/*.cpp src/*.hpp)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_executable(bittorrent ${SOURCE_FILES})

target_link_libraries(bittorrent PRIVATE OpenSSL::Crypto CURL::libcurl)
