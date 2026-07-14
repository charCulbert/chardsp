add_executable(chardsp_tests chardsp_Tests.cpp)
target_link_libraries(chardsp_tests PRIVATE chardsp)

add_test(NAME chardsp_tests COMMAND chardsp_tests)
