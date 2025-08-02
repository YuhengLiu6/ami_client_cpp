cmake_minimum_required(VERSION 3.15)
project(AmiClientCpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRDPARTY_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/thirdparty/usr/local/include")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Target system is Windows")
    set(CMAKE_PREFIX_PATH "C:/Users/YuhengLiu/vcpkg/installed/x64-windows")
    set(ASIO "asio")
    set(LIB_BOOST_ASIO "Boost::asio")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Target system is Linux")
else()
    message(FATAL_ERROR "Unsupported OS")
endif()

find_package(Boost REQUIRED CONFIG COMPONENTS ${ASIO} system)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${THIRDPARTY_INCLUDE_DIRS}
)

set(COMPILED_NAME "test")

file(GLOB_RECURSE SOURCES "src/*.cpp" "test/TestMsgType.cpp")

add_executable(${COMPILED_NAME} ${SOURCES})

target_link_libraries(${COMPILED_NAME} PRIVATE ${LIB_BOOST_ASIO} Boost::system OpenSSL::SSL OpenSSL::Crypto)

target_include_directories(${COMPILED_NAME} PRIVATE ${Boost_INCLUDE_DIRS})

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${COMPILED_NAME} PRIVATE -Wall -Wextra -O2)
endif()

# For Windows
if(WIN32)
    target_link_libraries(${COMPILED_NAME} ws2_32 wsock32)
endif()
