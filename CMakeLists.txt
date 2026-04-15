cmake_minimum_required(VERSION 3.16)

project(VibeSpeller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)

add_executable(VibeSpeller
    main.cpp
    database_manager.h
    database_manager.cpp
    audio_downloader.h
    audio_downloader.cpp
    gui_widgets.h
    gui_widgets_internal.h
    gui_widgets.cpp
    gui_widgets_internal.cpp
    gui_widgets_home_mapping.cpp
    gui_widgets_spelling.cpp
    gui_widgets_summary_stats_books.cpp
    gui_widgets_window.cpp
)

target_link_libraries(VibeSpeller PRIVATE Qt6::Widgets Qt6::Sql Qt6::Network)
target_compile_definitions(VibeSpeller PRIVATE
    VIBESPELLER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
