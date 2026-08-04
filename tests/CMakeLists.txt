add_executable(chardsp_tests
    chardsp_Tests.cpp
    chardsp_HeaderSecondTU.cpp)
target_link_libraries(chardsp_tests PRIVATE chardsp::chardsp)
