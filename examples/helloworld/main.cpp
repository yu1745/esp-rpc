#include "esprpc.h"
#include "esprpc_transport.h"
#include "hello_service.hpp"

extern "C" void app_main(void) {
    /* 初始化框架 */
    esprpc_init();

    /* 注册服务（宏生成的函数） */
    HelloService_register_rpc();

    /* 初始化 WebSocket 传输 */
    esprpc_transport_ws_init();
    esprpc_transport_t *ws = esprpc_transport_ws_get();
    if (ws) {
        esprpc_transport_add(ws);
        ws->start(ws->ctx,
            [](const uint8_t *data, size_t len, void *) {
                esprpc_handle_request(data, len);
            },
            nullptr);
    }

    /* 启动 HTTP 服务器（WiFi 获 IP 后调用，此处简化直接启动） */
    esprpc_transport_ws_start_server(nullptr, "/rpc");

    ESP_LOGI("main", "HelloService RPC ready at ws://<ip>/rpc");
    ESP_LOGI("main", "Methods:");
    ESP_LOGI("main", "  [0] SayHello(GreetRequest) -> GreetResponse");
    ESP_LOGI("main", "  [1] LogGreeting(GreetRequest)  [void]");
    ESP_LOGI("main", "  [2] StreamGreetings(int)  [stream]");
    ESP_LOGI("main", "  [3] Ping()  [void]");
}
