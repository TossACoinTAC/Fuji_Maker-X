cmake_minimum_required(VERSION 3.20)
project(fuji_ble_host_tests LANGUAGES C CXX)

set(CJSON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__cjson/cJSON")

add_executable(fuji_ble_test
    fuji_ble_test.cc
    ../../main/fuji_ble/fuji_ble_session.cc
    ../../main/fuji_protocol/fuji_protocol.cc
    ${CJSON_ROOT}/cJSON.c
)
target_compile_features(fuji_ble_test PRIVATE cxx_std_20)
target_include_directories(fuji_ble_test PRIVATE
    ../../main/fuji_ble
    ../../main/fuji_protocol
    ${CJSON_ROOT}
)
target_compile_definitions(fuji_ble_test PRIVATE
    FUJI_FIXTURE_ROOT="${CMAKE_CURRENT_LIST_DIR}/../../../../Shared/FujiProtocolV1/fixtures"
)
target_compile_options(fuji_ble_test PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME fuji_ble_test COMMAND fuji_ble_test)
