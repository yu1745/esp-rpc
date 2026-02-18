# ESP-IDF RPC 组件
idf_component_register(
    SRCS "src/esprpc.c" "src/esprpc_binary.c" "src/transport_ble.c" "src/transport_http_ws.c" "src/transport_serial.c"
    INCLUDE_DIRS "include" "."
    REQUIRES esp_timer esp_http_server bt driver
)

# 查找项目及组件内的 .rpc.hpp 文件
file(GLOB RPC_HEADERS
    "${CMAKE_SOURCE_DIR}/*.rpc.hpp"
    "${CMAKE_SOURCE_DIR}/*/*.rpc.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rpc.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*/*.rpc.hpp"
)

# 若有 .rpc.hpp 则添加生成目标
if(RPC_HEADERS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/generator/main.py")
    if(CONFIG_ESPRPC_TS_STUB_OUTPUT_DIR)
        set(ESPRPC_TS_OUT_DIR "${CONFIG_ESPRPC_TS_STUB_OUTPUT_DIR}")
        if(NOT IS_ABSOLUTE "${ESPRPC_TS_OUT_DIR}")
            set(ESPRPC_TS_OUT_DIR "${PROJECT_SOURCE_DIR}/${ESPRPC_TS_OUT_DIR}")
        endif()
        add_custom_target(esprpc_gen ALL
            COMMAND ${Python3_EXECUTABLE} ${GENERATOR}
                    -o ${ESPRPC_TS_OUT_DIR}
                    -t ${CONFIG_ESPRPC_RPC_CALL_TIMEOUT_MS}
                    ${RPC_HEADERS}
            DEPENDS ${RPC_HEADERS} ${GENERATOR}
            COMMENT "Generating RPC TS stubs"
        )
        add_dependencies(${COMPONENT_LIB} esprpc_gen)
    else()
        message(WARNING "CONFIG_ESPRPC_TS_STUB_OUTPUT_DIR is empty, TypeScript stubs will not be generated.")
        # 仅生成 C++ 端代码（.rpc.gen.hpp / .rpc.gen.cpp / .rpc.impl_user.cpp）
        add_custom_target(esprpc_gen_c_only ALL
            COMMAND ${Python3_EXECUTABLE} ${GENERATOR}
                    -t ${CONFIG_ESPRPC_RPC_CALL_TIMEOUT_MS}
                    ${RPC_HEADERS}
            DEPENDS ${RPC_HEADERS} ${GENERATOR}
            COMMENT "Generating RPC C++ code only (no TS stubs)"
        )
        add_dependencies(${COMPONENT_LIB} esprpc_gen_c_only)
    endif()
endif()
