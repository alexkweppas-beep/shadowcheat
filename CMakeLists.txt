cmake_minimum_required(VERSION 3.21)
project(shadowcheat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SHADOW_DEV_BUILD "Собрать с отладочными инструментами: логи, консоль, тестовое меню" OFF)

add_subdirectory(shared)
add_subdirectory(internal)
add_subdirectory(external)
