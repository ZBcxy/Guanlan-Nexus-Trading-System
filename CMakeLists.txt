# GNTS - 观澜 · Nexus Trading System
# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(GNTS VERSION 1.0.0 LANGUAGES C)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -D_GNU_SOURCE")

# 查找源文件
file(GLOB_RECURSE SOURCES "src/*.c")

# 创建静态库
add_library(gnts STATIC ${SOURCES})

# 设置头文件目录
target_include_directories(gnts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接库
target_link_libraries(gnts PUBLIC pthread m)

# 安装
install(TARGETS gnts
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include/gnts
)

# 测试
if(BUILD_TESTING)
    enable_testing()
    add_executable(test_main tests/test_main.c)
    target_link_libraries(test_main gnts)
    add_test(NAME GNTS_Test COMMAND test_main)
endif()

# 版本信息
set_target_properties(gnts PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# 输出信息
message(STATUS "GNTS Version: ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")