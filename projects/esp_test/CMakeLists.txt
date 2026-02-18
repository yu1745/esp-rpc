# ESP-IDF 测试工程
cmake_minimum_required(VERSION 3.16)

# 将 esprpc 的父目录加入，使 esprpc 目录作为组件被识别（组件名=目录名）
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esprpc_test)    