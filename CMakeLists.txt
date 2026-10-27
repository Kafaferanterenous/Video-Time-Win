cmake_minimum_required(VERSION 3.20)
project(videotime VERSION 1.2.0 LANGUAGES CXX)

add_executable(videotime WIN32 main.cpp media.cpp videotime.rc)
target_compile_features(videotime PRIVATE cxx_std_17)
target_compile_definitions(videotime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(videotime PRIVATE comctl32 comdlg32 shell32 uxtheme dwmapi mfplat mfreadwrite mfuuid ole32 propsys windowscodecs)

if(MSVC)
    set_property(TARGET videotime PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(videotime PRIVATE /W4 /utf-8)
endif()

enable_testing()
add_executable(videotime_tests tests.cpp media.cpp)
target_compile_features(videotime_tests PRIVATE cxx_std_17)
target_compile_definitions(videotime_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(videotime_tests PRIVATE mfplat mfreadwrite mfuuid ole32 propsys windowscodecs)
if(MSVC)
    set_property(TARGET videotime_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(videotime_tests PRIVATE /W4 /utf-8)
endif()
add_test(NAME videotime_model COMMAND videotime_tests)

add_custom_command(TARGET videotime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/portable"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:videotime>" "${CMAKE_BINARY_DIR}/portable/videotime.exe")
