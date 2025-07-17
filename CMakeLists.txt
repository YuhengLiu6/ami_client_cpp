cmake_minimum_required(VERSION 3.15)
project(AmiClientCpp)

# ✅ 设置 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ✅ 显式设置 Boost 的路径（如果不使用 CMAKE_TOOLCHAIN_FILE）
set(CMAKE_PREFIX_PATH "C:/Users/YuhengLiu/vcpkg/installed/x64-windows")
set(COMPILED_NAME "testClient")

# ✅ 使用 Boost 的 config 模式查找 Boost.Asio 和 Boost.System
find_package(Boost REQUIRED CONFIG COMPONENTS asio system)

# ✅ 打印调试信息（可选）
message(STATUS "Boost_INCLUDE_DIRS = ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_LIBRARIES    = ${Boost_LIBRARIES}")

# ✅ 自动收集源文件
file(GLOB_RECURSE SOURCES "src/*.cpp" "test/TestRawAmiClient.cpp")

# ✅ 生成可执行文件
add_executable(${COMPILED_NAME} ${SOURCES} "src/RawAmiClient.cpp")

# ✅ 链接 Boost 组件
target_link_libraries(${COMPILED_NAME} PRIVATE Boost::asio Boost::system)

# ✅ 添加 Boost 的头文件路径（部分配置中可能需要）
target_include_directories(${COMPILED_NAME} PRIVATE ${Boost_INCLUDE_DIRS})
