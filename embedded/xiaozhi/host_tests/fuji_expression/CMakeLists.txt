cmake_minimum_required(VERSION 3.20)
project(fuji_expression_host_tests LANGUAGES CXX)

enable_testing()

add_executable(fuji_expression_test
    fuji_expression_test.cc
    ../../main/boards/fuji-waveshare-1p46/fuji_expression_policy.cc
)
target_compile_features(fuji_expression_test PRIVATE cxx_std_20)
target_include_directories(fuji_expression_test PRIVATE
    ../../main/boards/fuji-waveshare-1p46
)
target_compile_options(fuji_expression_test PRIVATE -Wall -Wextra -Werror)

add_test(NAME fuji_expression_test COMMAND fuji_expression_test)
