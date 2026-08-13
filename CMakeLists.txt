cmake_minimum_required(VERSION 3.20)
project(videotime C)

add_executable(videotime WIN32 main.c videotime.rc)
target_compile_features(videotime PRIVATE c_std_11)
target_compile_definitions(videotime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(videotime PRIVATE comctl32 comdlg32 shell32)

if(MSVC)
    set_property(TARGET videotime PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(videotime PRIVATE /W4 /utf-8)
endif()

add_custom_command(TARGET videotime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/portable"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:videotime>" "${CMAKE_BINARY_DIR}/portable/videotime.exe")
