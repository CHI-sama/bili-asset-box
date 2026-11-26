# bili-asset-box/core/CMakeLists.txt
# 构建 C++ 内核 bili_core.dll（Windows）/ libbili_core.so（其他平台）
# 依赖：third_party/lib 下静态编译好的 libcurl（mbedTLS 后端）、mbedTLS、nlohmann/json 单头
cmake_minimum_required(VERSION 3.20)
project(bili_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 项目根目录（core 的上一级）
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(THIRD_PARTY "${PROJECT_ROOT}/third_party" CACHE PATH "第三方依赖目录")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CORE_SOURCES
    src/util.cpp
    src/http.cpp
    src/wbi.cpp
    src/api.cpp
    src/login.cpp
    src/job.cpp
    src/ratelimit.cpp
    src/feedback.cpp
    src/dispatch.cpp
    src/win_util.cpp
    src/abi.cpp
)

add_library(bili_core SHARED ${CORE_SOURCES})

target_include_directories(bili_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${THIRD_PARTY}/include          # nlohmann/json.hpp
            ${THIRD_PARTY}/include/mbedtls  # mbedTLS 头文件（如果安装到这里）
)

target_compile_definitions(bili_core PRIVATE
    BILI_CORE_EXPORTS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0601
    UNICODE
    _UNICODE
    CURL_STATICLIB
)

# ---------------------------------------------------------------- 第三方静态库
# 允许通过 -DCURL_LIB=... -DMBEDTLS_LIBS="a;b;c" 覆盖自动探测结果
find_library(CURL_LIB NAMES curl libcurl PATHS "${THIRD_PARTY}/lib" NO_DEFAULT_PATH)
if(NOT CURL_LIB)
    message(FATAL_ERROR "找不到 libcurl 静态库，请先运行 scripts/build_thirdparty.py")
endif()

find_library(MBEDTLS_LIB NAMES mbedtls PATHS "${THIRD_PARTY}/lib" NO_DEFAULT_PATH)
find_library(MBEDX509_LIB NAMES mbedx509 PATHS "${THIRD_PARTY}/lib" NO_DEFAULT_PATH)
find_library(MBEDCRYPTO_LIB NAMES mbedcrypto PATHS "${THIRD_PARTY}/lib" NO_DEFAULT_PATH)

set(SYS_LIBS ws2_32 crypt32 bcrypt advapi32 user32 shell32 iphlpapi)
if(MBEDTLS_LIB AND MBEDX509_LIB AND MBEDCRYPTO_LIB)
    list(APPEND SYS_LIBS ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB})
else()
    message(FATAL_ERROR "找不到 mbedTLS 静态库，请先运行 scripts/build_thirdparty.py")
endif()

find_package(Threads REQUIRED)
target_link_libraries(bili_core PRIVATE ${CURL_LIB} ${SYS_LIBS} Threads::Threads)

if(MINGW)
    # 把 libgcc/libstdc++/winpthread 全静态链进来，避免用户机器缺 MinGW 运行时
    # （-static 会连带 libwinpthread-1.dll 一起静态化，Python 端才能直接 CDLL 加载）
    target_link_options(bili_core PRIVATE -static -static-libgcc -static-libstdc++
                        -Wl,--no-insert-timestamp)
    target_compile_options(bili_core PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -Wno-unused-parameter)
endif()

# 输出到 build/bin，Python 端固定从这里加载
set_target_properties(bili_core PROPERTIES
    OUTPUT_NAME "bili_core"
    # MinGW 默认给库加 "lib" 前缀，Python 端按 bili_core.dll 加载，这里去掉
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_ROOT}/build/bin"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_ROOT}/build/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_ROOT}/build/lib"
)

# 可选的自测程序：core/tests/*.cpp（默认关闭，避免影响正常构建）
option(BUILD_CORE_TESTS "构建内核自测程序" OFF)
if(BUILD_CORE_TESTS)
    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    foreach(tsrc ${TEST_SOURCES})
        get_filename_component(tname ${tsrc} NAME_WE)
        add_executable(${tname} ${tsrc})
        target_include_directories(${tname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${THIRD_PARTY}/include)
        target_link_libraries(${tname} PRIVATE bili_core)
    endforeach()
endif()
