cmake_minimum_required(VERSION 3.15)
project(AmiClientCpp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRDPARTY_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/thirdparty/usr/local/include")

# vcpkg toolchain
set(CMAKE_TOOLCHAIN_FILE "C:/Users/YuhengLiu/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

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
)

if(WIN32)
    target_link_libraries(AmiClientCpp PUBLIC ws2_32 wsock32)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(AmiClientCpp PRIVATE -Wall -Wextra -O2)
endif()

# ─── Tests ──────────────────────────────────────────────────────────────────────
option(AMICLIENTCPP_BUILD_TESTS "Build test executable" ON)
if(AMICLIENTCPP_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "test/UnitTest.cpp")
    add_executable(AmiClientCppTests ${TEST_SOURCES})

    target_link_libraries(AmiClientCppTests
        PRIVATE
            AmiClientCpp
            GTest::gtest_main
    )

    target_include_directories(AmiClientCppTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    include(GoogleTest)
    gtest_discover_tests(AmiClientCppTests)
endif()
