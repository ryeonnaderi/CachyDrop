
cmake_minimum_required(VERSION 3.28)

project(CachyDrop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Quick
    Qml
    QuickControls2
)

find_package(KF6Kirigami REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(cachydrop
    src/main.cpp
    src/ReceiverServer.cpp
    src/ReceiverServer.h
)
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Quick
    Qml
    QuickControls2
    Network
)

qt_add_qml_module(cachydrop
    URI CachyDrop
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
)

target_link_libraries(cachydrop PRIVATE
    Qt6::Quick
    Qt6::Network
    Qt6::Qml
    Qt6::QuickControls2
    KF6::Kirigami
)

install(TARGETS cachydrop
    RUNTIME DESTINATION bin
)
