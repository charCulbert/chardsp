if (APPLE)
    add_library(chardsp_example_host STATIC common/DemoHost.cpp)
    target_link_libraries(chardsp_example_host PUBLIC chardsp
        "-framework WebKit"
        "-framework Cocoa"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreMIDI"
        "-framework CoreFoundation")
    target_compile_definitions(chardsp_example_host PRIVATE
        CHARDSP_DEMO_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/common")

    function(add_chardsp_example target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE chardsp_example_host)
    endfunction()

    add_chardsp_example(chardsp_oscillators oscillators/oscillators_Main.cpp)
    add_chardsp_example(chardsp_adsr adsr/adsr_Main.cpp)
    add_chardsp_example(chardsp_filter filter/filter_Main.cpp)
    add_chardsp_example(chardsp_noise noise/noise_Main.cpp)
    add_chardsp_example(chardsp_downsampler downsampler/downsampler_Main.cpp)
    add_chardsp_example(chardsp_delay_line delayLine/delayLine_Main.cpp)
    add_chardsp_example(chardsp_oversampler oversampler/oversampler_Main.cpp)
    add_chardsp_example(chardsp_voice_pool voicePool/voicePool_Main.cpp)
    add_chardsp_example(chardsp_mono_portamento monoPortamento/monoPortamento_Main.cpp)
    add_chardsp_example(chardsp_basic_synth_voice basicSynthVoice/basicSynthVoice_Main.cpp)
endif()
