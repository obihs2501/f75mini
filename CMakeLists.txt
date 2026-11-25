cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(f75mini VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This Windows desktop project requires MSVC and the Windows SDK.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(/W4 /utf-8 /permissive- /EHsc)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN=1 _WIN32_WINNT=0x0A00)

add_library(f75core STATIC src/model.cpp src/snapshot.cpp src/device.cpp)
target_include_directories(f75core PUBLIC src)
target_link_libraries(f75core PUBLIC hid setupapi)

add_executable(f75-cli src/cli.cpp)
target_link_libraries(f75-cli PRIVATE f75core)

add_executable(F75Config WIN32 src/gui.cpp src/app.manifest)
target_link_libraries(F75Config PRIVATE f75core comctl32 comdlg32 uxtheme user32 gdi32)

include(CTest)
if(BUILD_TESTING)
    add_executable(f75-model-tests tests/model_tests.cpp)
    target_link_libraries(f75-model-tests PRIVATE f75core)
    add_test(NAME model COMMAND f75-model-tests)
    add_test(NAME cli-help COMMAND f75-cli --help)
    add_test(NAME cli-contract COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.ps1" "$<TARGET_FILE:f75-cli>")
    add_test(NAME gui-startup COMMAND F75Config --smoke-test)
    set_tests_properties(model cli-help cli-contract gui-startup PROPERTIES TIMEOUT 25)
endif()

install(TARGETS f75-cli F75Config RUNTIME DESTINATION .)
install(FILES README.md docs/validation.md DESTINATION .)
