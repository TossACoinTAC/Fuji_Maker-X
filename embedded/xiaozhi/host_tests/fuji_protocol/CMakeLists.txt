cmake_minimum_required(VERSION 3.20)
project(fuji_protocol_host_test LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(XIAOZHI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(REPOSITORY_ROOT "${XIAOZHI_ROOT}/../..")
set(CJSON_ROOT "${XIAOZHI_ROOT}/managed_components/espressif__cjson/cJSON")

add_executable(fuji_protocol_host_test
    fuji_protocol_test.cc
    "${XIAOZHI_ROOT}/main/fuji_protocol/fuji_protocol.cc"
    "${CJSON_ROOT}/cJSON.c"
)
target_include_directories(fuji_protocol_host_test PRIVATE
    "${XIAOZHI_ROOT}/main/fuji_protocol"
    "${CJSON_ROOT}"
)
target_compile_definitions(fuji_protocol_host_test PRIVATE
    FUJI_FIXTURE_ROOT="${REPOSITORY_ROOT}/Shared/FujiProtocolV1/fixtures"
)
target_compile_options(fuji_protocol_host_test PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME fuji_protocol_host_test COMMAND fuji_protocol_host_test)
