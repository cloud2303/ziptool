cmake_minimum_required(VERSION 4.0)
project(ziptool)
set(CMAKE_CXX_STANDARD 23)


add_executable(${PROJECT_NAME} main.cpp)
add_subdirectory(zip)
add_subdirectory(indicators)
find_package(CLI11 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE zip::zip indicators::indicators CLI11::CLI11)