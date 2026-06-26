cmake_minimum_required(VERSION 3.31.6)
project(ziptool)
set(CMAKE_CXX_STANDARD 23)


add_executable(${PROJECT_NAME} main.cpp)
add_subdirectory(zip)
add_subdirectory(indicators)
find_package(CLI11 CONFIG REQUIRED)
target_sources(${PROJECT_NAME} PRIVATE FILE_SET CXX_MODULES FILES utils/compress.cppm utils/utils.cppm subcommand/zip.cppm)
target_link_libraries(${PROJECT_NAME} PRIVATE zip::zip indicators::indicators CLI11::CLI11)
