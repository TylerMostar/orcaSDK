cmake_minimum_required(VERSION 3.23)

project(orca_haptics_daemon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(orcaSDK
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..
)
FetchContent_MakeAvailable(orcaSDK)

add_executable(orca_haptics_daemon
    src/main.cpp
)

target_link_libraries(orca_haptics_daemon PRIVATE orcaSDK::core)

install(TARGETS orca_haptics_daemon
    RUNTIME DESTINATION bin
)
