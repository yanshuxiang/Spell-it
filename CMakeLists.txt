cmake_minimum_required(VERSION 3.16)

project(VibeSpeller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

add_executable(VibeSpeller
    main.cpp
    database_manager.h
    database_manager.cpp
    gui_widgets.h
    gui_widgets.cpp
)

target_link_libraries(VibeSpeller PRIVATE Qt6::Widgets Qt6::Sql)
