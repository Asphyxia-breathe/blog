cmake_minimum_required(VERSION 3.16)
project(blog_backend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找 Drogon 和 jwt-cpp
find_package(Drogon CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)

# 获取源文件列表
file(GLOB_RECURSE CTL_SRC "controllers/*.cc")
file(GLOB_RECURSE FILTER_SRC "filters/*.cc")
file(GLOB_RECURSE PLUGIN_SRC "plugins/*.cc")
file(GLOB_RECURSE MODEL_SRC "models/*.cc")

# 创建可执行文件
add_executable(${PROJECT_NAME}
    main.cc
    ${CTL_SRC}
    ${FILTER_SRC}
    ${PLUGIN_SRC}
    ${MODEL_SRC}
)

# 链接库
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Drogon::Drogon
        jwt-cpp::jwt-cpp
)

# 添加头文件搜索路径
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/models
        /home/vcpkg/installed/x64-linux/include
)