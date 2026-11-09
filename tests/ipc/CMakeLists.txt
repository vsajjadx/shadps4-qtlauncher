cmake_minimum_required(VERSION 3.24)
project(LauncherIpcRegressionTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
enable_testing()
add_executable(ipc_lifecycle_tests
    ipc_lifecycle_tests.cpp
    ../../src/ipc/ipc_client.cpp
    ../../src/ipc/ipc_client.h)
target_include_directories(ipc_lifecycle_tests PRIVATE stubs ../../src)
target_link_libraries(ipc_lifecycle_tests PRIVATE Qt6::Core Qt6::Gui)
if(WIN32)
    target_link_libraries(ipc_lifecycle_tests PRIVATE user32)
endif()
add_test(NAME ipc_lifecycle COMMAND ipc_lifecycle_tests)
set_tests_properties(ipc_lifecycle PROPERTIES TIMEOUT 90)
