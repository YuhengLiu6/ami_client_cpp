cmake_minimum_required(VERSION 3.15)
project(AmiClientCpp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRDPARTY_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/thirdparty/usr/local/include")

# vcpkg toolchain
#set(CMAKE_TOOLCHAIN_FILE "C:/Users/YuhengLiu/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(WARNING "CMAKE_TOOLCHAIN_FILE not set. If using vcpkg, please specify it manually via -DCMAKE_TOOLCHAIN_FILE=...")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Target system is Windows")
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
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ─── Library ────────────────────────────────────────────────────────────────────
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(AmiClientCpp STATIC ${LIB_SOURCES})

target_include_directories(AmiClientCpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${Boost_INCLUDE_DIRS}
        ${THIRDPARTY_INCLUDE_DIRS}
)

target_link_libraries(AmiClientCpp
    PUBLIC
        ${LIB_BOOST_ASIO}
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        fmt::fmt 
        spdlog::spdlog
)

if(WIN32)
    target_link_libraries(AmiClientCpp PUBLIC
        ws2_32
        wsock32
        crypt32
        bcrypt
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(AmiClientCpp PRIVATE -Wall -Wextra -O2)
endif()

#add_executable(Demo test/TestAutoFlush.cpp)
#add_executable(Demo test/TestMsgType.cpp)
add_executable(Demo test/TestMultiClient.cpp)
#add_executable(Demo test/TestAmiClient.cpp)
target_link_libraries(Demo
    PRIVATE
        AmiClientCpp
        fmt::fmt
        spdlog::spdlog
)
target_include_directories(Demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)


set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Demo)

# ─── Tests ──────────────────────────────────────────────────────────────────────

option(AMICLIENTCPP_BUILD_TESTS "Build test executable" OFF)
if(AMICLIENTCPP_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()


    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "test/gtests/*.cpp")

    add_executable(AmiClientCppTests ${TEST_SOURCES})

    target_link_libraries(AmiClientCppTests
        PRIVATE
            AmiClientCpp
            GTest::gtest
            GTest::gtest_main
            fmt::fmt   
            spdlog::spdlog
    )

    target_include_directories(AmiClientCppTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    include(GoogleTest)
    gtest_discover_tests(AmiClientCppTests)
endif()
