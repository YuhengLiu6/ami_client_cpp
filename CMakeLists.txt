cmake_minimum_required(VERSION 3.15)
project(AmiClientCpp)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH "C:/Users/YuhengLiu/vcpkg/installed/x64-windows")
set(COMPILED_NAME "testAutoFlush")


find_package(Boost REQUIRED CONFIG COMPONENTS asio system)
find_package(nlohmann_json CONFIG REQUIRED)

message(STATUS "Boost_INCLUDE_DIRS = ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_LIBRARIES    = ${Boost_LIBRARIES}")

file(GLOB_RECURSE SOURCES "src/*.cpp" "test/TestAutoFlush.cpp")


add_executable(${COMPILED_NAME} ${SOURCES})


target_link_libraries(${COMPILED_NAME} PRIVATE Boost::asio Boost::system)

target_include_directories(${COMPILED_NAME} PRIVATE ${Boost_INCLUDE_DIRS})
