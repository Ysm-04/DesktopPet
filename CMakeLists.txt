cmake_minimum_required(VERSION 3.20)

project(QtDesktopPet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(QtDesktopPet
    main.cpp
    src/ui/PetWindow.h
    src/ui/PetWindow.cpp
)

target_compile_definitions(QtDesktopPet
    PRIVATE
        PET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(QtDesktopPet
    PRIVATE
        Qt6::Widgets
)

if (WIN32)
    target_compile_definitions(QtDesktopPet PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
