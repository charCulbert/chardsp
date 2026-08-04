add_executable(chardsp_demo_runner demoRunner_Main.cpp)
target_link_libraries(chardsp_demo_runner PRIVATE chardsp)
target_compile_definitions(chardsp_demo_runner PRIVATE
    CHARDSP_DEMO_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/..")

if (APPLE)
    target_link_libraries(chardsp_demo_runner PRIVATE
        "-framework WebKit"
        "-framework Cocoa"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreMIDI"
        "-framework CoreFoundation")
endif()
