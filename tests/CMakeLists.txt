add_executable(chardsp_tests chardsp_Tests.cpp)
target_link_libraries(chardsp_tests PRIVATE chardsp::chardsp chardsp_choc)
