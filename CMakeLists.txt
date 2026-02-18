# ESP-IDF RPC 组件
idf_component_register(
    SRCS "src/esprpc.c" "src/esprpc_binary.c" "src/transport_ble.c" "src/transport_http_ws.c"
    INCLUDE_DIRS "include" "."
    REQUIRES esp_timer esp_http_server
)

# TS stub 输出目录，可由项目覆盖
if(NOT DEFINED ESPRPC_TS_STUB_OUTPUT_DIR)
    set(ESPRPC_TS_STUB_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../ts/generated" CACHE PATH "TS stub output directory")
endif()

# 查找项目及组件内的 .rpc.h 文件
file(GLOB RPC_HEADERS
    "${CMAKE_SOURCE_DIR}/*.rpc.h"
    "${CMAKE_SOURCE_DIR}/*/*.rpc.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rpc.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*/*.rpc.h"
)

# 若有 .rpc.h 则添加生成目标
if(RPC_HEADERS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/generator/main.py")
    add_custom_target(esprpc_gen ALL
        COMMAND ${Python3_EXECUTABLE} ${GENERATOR}
                -o ${ESPRPC_TS_STUB_OUTPUT_DIR}
                -t ${CONFIG_ESPRPC_RPC_CALL_TIMEOUT_MS}
                ${RPC_HEADERS}
        DEPENDS ${RPC_HEADERS} ${GENERATOR}
        COMMENT "Generating RPC TS stubs"
    )
    add_dependencies(${COMPONENT_LIB} esprpc_gen)
endif()
