cmake_minimum_required(VERSION 3.16)
project(REXTPROXY VERSION 1.4.5.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(dimensions
    main.cpp
    ProxyServer.cpp
    WarpManager.cpp
    CommandHandler.cpp
    PlayerRules.cpp
)

target_include_directories(dimensions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dimensions PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(dimensions PRIVATE ws2_32)
else()
    target_link_libraries(dimensions PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(dimensions PRIVATE /W4 /utf-8)
else()
    target_compile_options(dimensions PRIVATE -O0 -Wall -Wextra -Wpedantic)
    # The proxy is I/O-bound. O0 keeps GCC builds predictable across machines and
    # avoids very slow optimizer passes over the protocol-routing state machine.
    set_source_files_properties(ProxyServer.cpp PROPERTIES COMPILE_OPTIONS "-O0")
endif()

add_custom_command(TARGET dimensions POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:dimensions>/config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml" "$<TARGET_FILE_DIR:dimensions>/config.yaml"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dimensions>/logs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dimensions>/extensions"
)

install(TARGETS dimensions DESTINATION bin)
install(FILES config.json config.yaml README.md V4-MIGRATION.md REALIP-SETUP.md DESTINATION bin)
install(DIRECTORY extensions/ DESTINATION bin/extensions)

include(CTest)
if(BUILD_TESTING)
    add_executable(protocol-tests tests/ProtocolUnitTests.cpp PlayerRules.cpp)
    target_include_directories(protocol-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(NOT MSVC)
        # The header-heavy test translation unit compiles substantially faster at O0.
        set_source_files_properties(tests/ProtocolUnitTests.cpp PROPERTIES COMPILE_OPTIONS "-O0")
    endif()
    add_test(NAME protocol-unit-tests COMMAND protocol-tests)
endif()

if(BUILD_TESTING)
    add_executable(ban-tests tests/BanTests.cpp)
    target_include_directories(ban-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ban-persistence COMMAND ban-tests)
endif()

set_target_properties(dimensions PROPERTIES ENABLE_EXPORTS ON)
if(MINGW)
    target_link_options(dimensions PRIVATE "-Wl,--export-all-symbols")
endif()
add_library(PlayTime MODULE extensions/PlayTime/PlayTimeExtension.cpp)
set_target_properties(PlayTime PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions")
if(WIN32)
    target_link_libraries(PlayTime PRIVATE dimensions ws2_32)
endif()
