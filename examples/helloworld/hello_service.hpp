#include "esprpc.hpp"
#include "esp_log.h"

/* ================================================================
 * 1. 定义数据结构 + 自动序列化（一个宏搞定结构体+Serializer）
 * ================================================================ */

ESPRPC_STRUCT(GreetRequest,
    (name, esprpc::StringBuf)
)

ESPRPC_STRUCT(GreetResponse,
    (code, int32_t),
    (message, esprpc::StringBuf)
)

/* ================================================================
 * 2. 实现 RPC 服务（就是个普通 C++ 类）
 * ================================================================ */

class HelloService {
public:
    GreetResponse SayHello(GreetRequest req) {
        GreetResponse resp{};
        resp.code = 0;
        resp.message.setf("Hello, %s!", req.name.c_str());
        ESP_LOGI("HelloService", "SayHello: %s", resp.message.c_str());
        return resp;
    }

    void LogGreeting(GreetRequest req) {
        ESP_LOGI("HelloService", "LogGreeting: %s", req.name.c_str());
    }

    rpc_stream<GreetResponse> StreamGreetings(int32_t count) {
        uint16_t mid = esprpc_get_stream_method_id();
        if (mid == ESPRPC_STREAM_METHOD_ID_NONE) return {nullptr};
        for (int32_t i = 0; i < count; i++) {
            GreetResponse resp{};
            resp.code = i;
            resp.message.setf("Greeting #%d", i + 1);
            esprpc::stream_emit(mid, resp);
        }
        return {nullptr};
    }

    void Ping() {
        ESP_LOGI("HelloService", "pong");
    }
};

static HelloService s_hello_service;

ESPRPC_SERVICE(HelloService, s_hello_service,
    esprpc::method<HelloService, &HelloService::SayHello>(0),
    esprpc::method<HelloService, &HelloService::LogGreeting>(1, esprpc::MF_VOID),
    esprpc::method<HelloService, &HelloService::StreamGreetings>(2, esprpc::MF_STREAM),
    esprpc::method<HelloService, &HelloService::Ping>(3, esprpc::MF_VOID)
)
