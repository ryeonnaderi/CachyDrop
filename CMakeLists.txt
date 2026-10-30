cmake_minimum_required(VERSION 3.28)

project(CachyDrop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Quick
    Qml
    QuickControls2
    Network
    Widgets
)

find_package(KF6Kirigami REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(QRENCODE REQUIRED IMPORTED_TARGET libqrencode)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(cachydrop
    src/main.cpp
    src/ReceiverServer.cpp
    src/ReceiverServer.h
    src/WebPages.h
    src/WebPages.cpp
    src/HttpUtils.h
    src/HttpUtils.cpp
    src/TransferUtils.h
    src/TransferUtils.cpp
    src/PairingManager.cpp
    src/PairingManager.h
    src/Network.h 
    src/Network.cpp
    src/QrCodeUtils.h
    src/QrCodeUtils.cpp
    src/HttpRequestParser.h
    src/HttpRequestParser.cpp
)

qt_add_qml_module(cachydrop
    URI CachyDrop
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
)

target_link_libraries(cachydrop PRIVATE
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
    KF6::Kirigami
    PkgConfig::QRENCODE
    PkgConfig::SODIUM
)

install(TARGETS cachydrop
    RUNTIME DESTINATION bin
)