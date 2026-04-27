# ESP-IDF RPC 框架 (esp-rpc)

H5/TypeScript 与 ESP32 之间的嵌入式 RPC 通信框架，支持 **WebSocket**、**BLE**、**串口**三种传输，使用 C++ 模板元编程在编译期自动生成序列化与分发代码。

## 特性

- **零开销抽象** — 序列化和方法分发全部在编译期由模板和宏完成，运行时只有函数指针跳转，无虚表、无反射、无动态内存分配（响应体除外）
- **免代码生成** — 使用 `ESPRPC_STRUCT` + `ESPRPC_SERVICE` 宏在头文件中一站式声明类型和服务，编译期自动展开，无需额外代码生成步骤（TypeScript 客户端仍可选择性使用生成器）
- **三传输支持** — WebSocket、BLE（NimBLE）、串口（UART/USB Serial/JTAG），对上层完全透明
- **流式推送** — 服务端可主动向客户端推送数据流，适用于实时监控、事件通知等场景
- **ESP-IDF 组件** — 标准 IDF 组件，通过 `idf_component.yml` 注册，`menuconfig` 配置
- **TypeScript 客户端生成器** — Python 脚本解析 C++ 头文件，自动生成类型定义、序列化函数和客户端类

## 目录结构

```
esprpc/
├── include/                      # 头文件
│   ├── esprpc.h                  #   C API
│   ├── esprpc.hpp                #   C++ 模板 + 宏（核心）
│   ├── esprpc_binary.h           #   二进制协议原语
│   ├── esprpc_service.h          #   服务注册 API
│   └── esprpc_transport.h        #   传输层抽象接口
├── src/                          # C 实现
│   ├── esprpc.c                  #   核心引擎：服务注册、请求分发、内存池
│   ├── esprpc_binary.c           #   二进制读写原语
│   ├── transport_http_ws.c       #   WebSocket 传输
│   ├── transport_ble.c           #   BLE (NimBLE) 传输
│   └── transport_serial.c        #   串口传输
├── generator/
│   └── main.py                   # TypeScript 客户端代码生成器
├── examples/
│   └── helloworld/               # 最小 Hello World 示例
│       ├── hello_service.hpp     #   服务定义
│       ├── main.cpp              #   入口
│       ├── CMakeLists.txt
│       └── sdkconfig.defaults
├── projects/
│   ├── esp_test/                 # ESP-IDF 全功能测试工程
│   │   ├── main/
│   │   │   ├── user_service.hpp  #     演示服务（8 个方法）
│   │   │   ├── main.cpp          #     入口（WiFi + 三传输）
│   │   │   └── wifi_sta.cpp/hpp  #     WiFi STA 事件处理
│   │   └── sdkconfig.defaults
│   ├── preact-app/               # Preact + Vite 前端 DEMO
│   │   ├── src/
│   │   │   ├── generated/        #     生成客户端代码
│   │   │   ├── app.tsx           #     UI
│   │   │   └── performance-test.ts
│   │   └── package.json
│   └── ts_test/                  # 纯 TS 测试（Node.js + tsx）
│       ├── rpc_types.ts          #   生成：类型 + 序列化函数
│       ├── rpc_client.ts         #   生成：客户端类
│       ├── transport-ws.ts       #   WebSocket 传输实现
│       └── main.ts               #   测试脚本
├── Kconfig                       # 组件配置
├── CMakeLists.txt                # 组件构建
└── idf_component.yml             # 组件注册
```

## 快速开始

### 1. 定义 RPC 服务

创建 `hello_service.hpp`：

```cpp
#include "esprpc.hpp"

// 定义数据结构 + 自动序列化
ESPRPC_STRUCT(GreetRequest,
    (name, esprpc::StringBuf)
)

ESPRPC_STRUCT(GreetResponse,
    (code, int32_t),
    (message, esprpc::StringBuf)
)

// 服务实现（普通 C++ 类）
class HelloService {
public:
    GreetResponse SayHello(GreetRequest req) {
        GreetResponse resp{};
        resp.code = 0;
        resp.message.setf("Hello, %s!", req.name.c_str());
        return resp;
    }

    void LogGreeting(GreetRequest req) {
        ESP_LOGI("HelloService", "LogGreeting: %s", req.name.c_str());
    }

    rpc_stream<GreetResponse> StreamGreetings(int32_t count) {
        uint16_t mid = esprpc_get_stream_method_id();
        for (int32_t i = 0; i < count; i++) {
            GreetResponse resp{};
            resp.code = i;
            resp.message.setf("Greeting #%d", i + 1);
            esprpc::stream_emit(mid, resp);
        }
        return {nullptr};
    }

    void Ping() { ESP_LOGI("HelloService", "pong"); }
};

static HelloService s_hello_service;

// 注册服务（宏展开生成 dispatch 函数 + 方法表 + 注册函数）
ESPRPC_SERVICE(HelloService, s_hello_service,
    esprpc::method<HelloService, &HelloService::SayHello>(0),
    esprpc::method<HelloService, &HelloService::LogGreeting>(1, esprpc::MF_VOID),
    esprpc::method<HelloService, &HelloService::StreamGreetings>(2, esprpc::MF_STREAM),
    esprpc::method<HelloService, &HelloService::Ping>(3, esprpc::MF_VOID)
)
```

### 2. 初始化框架

```cpp
#include "hello_service.hpp"

extern "C" void app_main() {
    esprpc_init();
    HelloService_register_rpc();    // 由 ESPRPC_SERVICE 宏生成

    // 初始化 WebSocket 传输
    esprpc_transport_ws_init();
    esprpc_transport_add(esprpc_transport_ws_get());
    esprpc_transport_ws_start_server(nullptr, "/rpc");
}
```

### 3. 构建

```bash
cd examples/helloworld
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

### 4. TypeScript 客户端

```typescript
import { UserServiceClient } from './rpc_client';
import { createWebSocketTransport } from './transport-ws';

const transport = createWebSocketTransport('ws://192.168.4.1/rpc');
const client = new UserServiceClient(transport);
await transport.connect();

// 普通调用
const user = await client.GetUser(123);

// 流式调用
client.WatchUsers().subscribe((u) => console.log('stream:', u));

// 即发即忘（VOID）
client.Ping();
```

---

## C++ API 参考

### 基础类型

| 类型 | 序列化 | 说明 |
|------|--------|------|
| `int32_t` | 4B LE | 有符号 32 位整数 |
| `uint32_t` | 4B LE | 无符号 32 位整数 |
| `int16_t` / `uint16_t` | 2B LE | 16 位整数 |
| `uint8_t` | 1B | 单字节 |
| `bool` | 1B | 0/非0 |
| `int` | 4B LE | 等价 int32_t |

所有基本类型的 `Serializer` 特化由 `ESPRPC_PRIMITIVE` 宏生成，位于 `esprpc.hpp:106-123`。

### 字符串：`esprpc::StringBuf`

固定 128 字节的字符串缓冲区，不涉及堆分配：

```cpp
struct StringBuf {
    static constexpr size_t MAX = 128;
    char data[MAX];
    size_t len = 0;

    const char *c_str() const;
    StringBuf &operator=(const char *s);
    void setf(const char *fmt, ...);   // 格式化赋值
    bool operator==(const char *s) const;
};
```

二进制格式：`[2B 长度 LE][UTF-8 字节]`（`esprpc.hpp:159-171`）。

### 可选值：`esprpc::Optional<T>`

```cpp
template <typename T>
struct Optional {
    bool present = false;
    T value{};
    Optional &operator=(const T &v);
};
```

二进制格式：`[1B present][T 值（如果 present）]`（`esprpc.hpp:184-194`）。

### 列表：`esprpc::List<T>`

```cpp
template <typename T>
struct List {
    T *items = nullptr;    // 由调用者管理的内存
    size_t len = 0;         // 写入时=元素数；读取时输入=容量，输出=实际个数
};
```

二进制格式：`[4B 数量 LE][T 值序列]`。

读取时如果 `items == nullptr` 则跳过所有元素；如果 `items != nullptr` 则 `len` 作为容量上限，超出部分丢弃（`esprpc.hpp:209-226`）。

### 流式返回：`rpc_stream<T>`

```cpp
template<typename T>
struct rpc_stream {
    void *ctx;
};
```

作为方法的返回类型，表示该方法会通过 `esprpc::stream_emit()` 推送多条数据。

### 结构体自动序列化：`ESPRPC_STRUCT` 宏

```cpp
ESPRPC_STRUCT(StructName,
    (field1, Type1),
    (field2, Type2),
    ...
)
```

等价于（`esprpc.hpp:355-369`）：

```cpp
struct StructName {
    Type2 field1;
    Type2 field2;
    ...
};

namespace esprpc {
template <>
struct Serializer<StructName> {
    static void write(Buffer &buf, const StructName &v) {
        ::esprpc::write(buf, v.field1);
        ::esprpc::write(buf, v.field2);
        ...
    }
    static void read(Reader &r, StructName &v) {
        ::esprpc::read(r, v.field1);
        ::esprpc::read(r, v.field2);
        ...
    }
};
}
```

字段数上限：16。如需为已有 struct 添加序列化，使用 `ESPRPC_SERIALIZE(StructName, field1, field2, ...)`。

### 服务注册：`ESPRPC_SERVICE` 宏

```cpp
ESPRPC_SERVICE(ServiceName, impl_instance,
    esprpc::method<ServiceName, &ServiceName::Method0>(method_id, [flags]),
    esprpc::method<ServiceName, &ServiceName::Method1>(method_id, [flags]),
    ...
)
```

宏展开为三部分（`esprpc.hpp:406-427`）：

1. **静态方法表**：`esprpc::MethodInfo _esprpc_ServiceName_methods[]` — 数组，每个元素包含 `id`、`flags`、`dispatch` 函数指针
2. **Service 级 dispatch 函数**：`_esprpc_ServiceName_dispatch()` — 遍历方法表，按 `method_id & 0x1F` 匹配
3. **注册函数**：`ServiceName_register_rpc()` — 调用 `esprpc_register_service_ex()`

#### MethodFlag

| 标志 | 值 | 说明 |
|------|-----|------|
| `MF_NONE` | 0 | 默认。客户端等待响应，服务端返回值序列化后返回 |
| `MF_VOID` | 1 << 0 | 即发即忘。服务端不返回响应体 |
| `MF_STREAM` | 1 << 1 | 流式推送。服务端通过 `stream_emit` 多次发送数据 |

#### `esprpc::method<C, Method>(id, flags)`

工厂函数，返回 `MethodInfo`：

```cpp
template <typename C, auto Method>
MethodInfo method(uint16_t id, uint32_t flags = 0);
```

`C` 是服务类，`Method` 是成员函数指针。编译期通过 `auto` 推导成员函数类型。

### 流式推送：`esprpc::stream_emit<T>`

```cpp
template <typename T>
void stream_emit(uint16_t method_id, const T &v);
```

序列化 `v` 并通过指定 `method_id` 发送流式帧（`invoke_id = 0`）。典型用法：

```cpp
rpc_stream<GreetResponse> StreamGreetings(int32_t count) {
    uint16_t mid = esprpc_get_stream_method_id();  // 从 dispatch 上下文获取 method_id
    for (...) {
        GreetResponse resp{...};
        esprpc::stream_emit(mid, resp);
    }
    return {nullptr};
}
```

### Serializer 模板

```cpp
template <typename T>
struct Serializer {
    static void write(Buffer &buf, const T &v);
    static void read(Reader &r, T &v);
};
```

内置特化：基本类型、`StringBuf`、`Optional<T>`、`List<T>`、`ESPRPC_STRUCT` 生成的类型。可通过特化 `Serializer` 为自定义类型添加序列化支持。

### Buffer / Reader

- **`esprpc::Buffer`** — 动态增长写缓冲区（`realloc`），用于序列化输出。提供 `write_u8/u16/u32/i32()` 和 `data()/size()`
- **`esprpc::Reader`** — 只读字节流包装器，用于反序列化输入。提供 `read_u8/u16/u32/i32/bool()` 和 `error()`/`reset()` 错误检查

---

## 模板 Dispatch 架构

框架的请求分发有三层间接跳转，全部在编译期生成：

```
传输层收到二进制帧
    → esprpc_handle_request()          [C 运行时: 按 method_id>>5 查服务表]
        → _esprpc_<Svc>_dispatch()     [宏生成: 按 method_id&0x1F 遍历方法表]
            → MethodDispatch::dispatch  [模板: 反序列化→调用→序列化]
                → Service::Method()     [用户业务逻辑]
```

### 第一层：C 运行时 (esprpc.c:246-291)

`esprpc_handle_request()` 解析帧头：
- `method_id` 高 3 位 → 服务索引（最多 8 个服务）
- `method_id` 低 5 位 → 方法索引
- 查 `s_services[]` 表，调用对应服务的 `dispatch` 函数指针

### 第二层：宏生成 (esprpc.hpp:406-427)

`ESPRPC_SERVICE` 展开为线性遍历方法表的 dispatch 函数：
```c
static int _esprpc_HelloService_dispatch(...) {
    uint8_t mth = method_id & 0x1F;
    for (size_t i = 0; i < NUM_METHODS; i++) {
        if (methods[i].id == mth)
            return methods[i].dispatch(method_id, req, req_len, resp, resp_len, impl);
    }
    return -1;  // 方法未找到
}
```

### 第三层：MethodDispatch 模板 (esprpc.hpp:268-309)

核心 per-method dispatch，编译期为每个方法生成特化：

```cpp
template <typename C, typename R, typename... Args, R (C::*Method)(Args...)>
struct MethodDispatch<C, Method> {
    static int dispatch(...) {
        auto *self = static_cast<C *>(impl);
        Reader reader(req, req_len);

        // 编译期：用折叠表达式反序列化参数元组
        using ArgStorage = std::tuple<Args...>;
        ArgStorage args{};
        read_tuple(reader, args);           // fold 展开为顺序 read 调用
        if (reader.error()) return -1;       // 反序列化失败

        // 编译期分支：根据返回值类型选择行为
        if constexpr (is_stream) {           // 流式
            esprpc_set_stream_method_id(method_id);
            (self->*Method)(args...);
            esprpc_set_stream_method_id(ESPRPC_STREAM_METHOD_ID_NONE);
        } else if constexpr (is_void) {      // 即发即忘
            (self->*Method)(args...);
        } else {                             // 普通调用
            auto result = (self->*Method)(args...);
            Serializer<R>::write(buf, result);
            *resp = malloc(buf.size());
            memcpy(*resp, buf.data(), buf.size());
        }
        return 0;
    }
};
```

关键编译期技术：
- `std::index_sequence` + fold 表达式展开 `read_tuple` —— 无论多少个参数，都在编译期展开为顺序 read
- `if constexpr` 根据 `is_void_return<R>` 和 `is_rpc_stream<R>` 在编译期选择分支
- `auto Method` 非类型模板参数推导成员函数指针类型

---

## 二进制协议

### 帧格式

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  method_id    |         invoke_id (LE)        |  payload_len  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  payload_len  |                                               |
+-+-+-+-+-+-+-+-+                                               |
|                                                               |
|                        binary payload                        |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **method_id** (1B): 高 3 位 = 服务索引 (0-7)，低 5 位 = 方法索引 (0-31)
- **invoke_id** (2B LE): 0 = 流式推送，非 0 = 请求-响应匹配（客户端自动递增）
- **payload_len** (2B LE): payload 长度
- **payload** (N bytes): 序列化参数/返回值

### 编码规则

| 类型 | 编码 |
|------|------|
| `int32_t` / `uint32_t` / `int` | 4 字节小端 |
| `int16_t` / `uint16_t` | 2 字节小端 |
| `uint8_t` / `bool` | 1 字节 |
| `StringBuf` / string | [2B 长度 LE][UTF-8 字节] |
| `Optional<T>` | [1B present 标记][T 值（仅 present=1 时）] |
| `List<T>` | [4B 数量 LE][T 值序列] |
| 结构体 (ESPRPC_STRUCT) | 字段按声明顺序依次编码 |

### 帧类型

| invoke_id | 方向 | 说明 |
|-----------|------|------|
| 非 0 | 请求 (C→S) | 客户端发起调用，invoke_id 用于匹配响应 |
| 非 0 | 响应 (S→C) | 服务端返回结果，invoke_id 回显请求中的值 |
| 0 | 流式推送 (S→C) | 服务端主动推送，接收方按 method_id 分发到对应 subscribe |

---

## 传输层

### 传输抽象接口

```c
typedef struct esprpc_transport {
    esp_err_t (*send)(void *ctx, const uint8_t *data, size_t len);
    esp_err_t (*start)(void *ctx, esprpc_transport_on_recv_fn on_recv, void *user_ctx);
    void (*stop)(void *ctx);
    void *ctx;
} esprpc_transport_t;
```

所有传输实现 `send/start/stop`，收到帧时调用 `on_recv(data, len, user_ctx)`，再由 `esprpc_handle_request()` 处理。

### WebSocket

- 基于 `esp_http_server`
- 默认端点为 `/rpc`
- 支持外部注入已有 `httpd_handle_t`
- 异步发送使用 `httpd_ws_send_data_async` 避免死锁
- 启用条件：`CONFIG_HTTPD_WS_SUPPORT=y`

### BLE (NimBLE)

- 自定义 Service UUID：`0000E530-1212-EFDE-1523-785FEABCD123`
- TX Characteristic (Write)：`0000E531-...` — 客户端 → ESP32 请求
- RX Characteristic (Notify)：`0000E532-...` — ESP32 → 客户端响应
- 单连接，断线自动重广告
- 设备名：`ESPRPC`
- 启用条件：`CONFIG_ESPRPC_ENABLE_BLE=y` + `CONFIG_BT_ENABLED=y` + `CONFIG_BT_NIMBLE_ENABLED=y`

### 串口 (UART/USB Serial/JTAG)

- 不管理 UART 初始化，由应用层提供 tx 回调
- 帧同步通过可配置的前缀/后缀字节序列实现
- `Kconfig` 可配置前缀（如 `\xAB\xCD`）和后缀
- 应用通过 `esprpc_serial_feed_packet()` / `esprpc_serial_feed_raw_packet()` 喂数据
- 启用条件：`CONFIG_ESPRPC_ENABLE_SERIAL=y`

---

## TypeScript 客户端

### Transport 接口

```typescript
interface EsprpcTransport {
    call(methodId: number, payload: Uint8Array, options?: { timeout?: number }): Promise<Uint8Array>;
    subscribe(methodId: number, cb: (data: Uint8Array) => void): void;
    unsubscribe(methodId: number): void;
    connect(): Promise<void>;
    disconnect(): void;
}
```

内置实现：`transport-ws.ts`（WebSocket）。BLE 和串口实现在 `preact-app/src/generated/` 中。

### 生成客户端

通过 `generator/main.py` 从 C++ 头文件生成（`esprpc.hpp:160-239` 解析逻辑）：

```bash
python3 generator/main.py -o output_dir path/to/service.hpp
```

生成两个文件：

- **`rpc_types.ts`** — 接口定义 + `write`/`read` 序列化函数
- **`rpc_client.ts`** — 带类型方法的客户端类

生成的服务方法签名：

| C++ 返回类型 | TypeScript 方法签名 |
|-------------|-------------------|
| `T`（普通） | `method(params) => Promise<T>` |
| `void`（MF_VOID） | `method(params) => void`（不等待） |
| `rpc_stream<T>`（MF_STREAM） | `method(params) => { subscribe: (cb) => void }` |

---

## 配置项 (Kconfig)

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `ESPRPC_POOL_BLOCK_SIZE` | 2048 | 内存池块大小（帧缓冲区） |
| `ESPRPC_RPC_CALL_TIMEOUT_MS` | 2000 | TypeScript 调用超时 |
| `ESPRPC_TS_STUB_OUTPUT_DIR` | (空) | 生成 TypeScript 客户端输出目录 |
| `ESPRPC_ENABLE_BLE` | n | 启用 BLE 传输 |
| `ESPRPC_ENABLE_WS` | y | 启用 WebSocket 传输 |
| `ESPRPC_ENABLE_SERIAL` | n | 启用串口传输 |
| `ESPRPC_SERIAL_PAYLOAD_MAX` | 2048 | 串口帧负载上限 |
| `ESPRPC_SERIAL_PREFIX` | (空) | 串口帧前缀（用于定界） |
| `ESPRPC_SERIAL_SUFFIX` | (空) | 串口帧后缀 |

---

## 测试工程

### ESP-IDF 测试工程 (`projects/esp_test`)

全功能示例，包含 8 个 RPC 方法的 `UserService`：
- `UserStatus` 枚举 + 三个 `ESPRPC_STRUCT` 类型
- `GetUser`、`CreateUser`、`CreateUserV2`(VOID)、`UpdateUser`、`DeleteUser`、`ListUsers`(返回 `List<User>`)、`WatchUsers`(STREAM)、`Ping`(VOID)
- WebSocket + BLE + 串口三传输并行

```bash
cp projects/esp_test/main/wifi_config_local.h.example \
   projects/esp_test/main/wifi_config_local.h
# 编辑 wifi_config_local.h 填入 WiFi 凭据
cd projects/esp_test
idf.py set-target esp32c3
idf.py menuconfig   # 配置传输层
idf.py build flash monitor
```

### Preact 前端 (`projects/preact-app`)

带 UI 的浏览器 DEMO，支持 WebSocket/BLE/Serial 切换，包含性能测试面板：

```bash
cd projects/preact-app
pnpm install
pnpm dev
```

### TypeScript 测试 (`projects/ts_test`)

纯 Node.js 端到端测试，验证所有 RPC 方法：

```bash
cd projects/ts_test
pnpm install
pnpm start         # 默认连接 ws://192.168.4.1/rpc
```

---

## API 速查

### C API (esprpc.h)

| 函数 | 说明 |
|------|------|
| `esprpc_init()` | 初始化框架（内存池、互斥锁） |
| `esprpc_deinit()` | 反初始化 |
| `esprpc_register_service(name, impl, dispatch)` | 注册服务 |
| `esprpc_handle_request(data, len)` | 处理接收到的 RPC 帧 |
| `esprpc_send(data, len)` | 广播数据到所有传输层 |
| `esprpc_transport_add(t)` | 注册传输层实例 |
| `esprpc_set_stream_method_id(id)` | 设置当前 stream method_id |
| `esprpc_get_stream_method_id()` | 获取当前 stream method_id |
| `esprpc_stream_emit(method_id, data, len)` | 发送流式帧 |

### C++ 模板 (esprpc.hpp)

| API | 说明 |
|------|------|
| `ESPRPC_STRUCT(T, (field, Type)...)` | 定义结构体 + 自动序列化 |
| `ESPRPC_SERIALIZE(T, fields...)` | 为已有结构体添加序列化 |
| `ESPRPC_SERVICE(NAME, IMPL, methods...)` | 注册 RPC 服务 |
| `esprpc::method<C, &C::fn>(id, flags)` | 创建 MethodInfo |
| `esprpc::stream_emit<T>(mid, v)` | 类型安全的流式推送 |
| `esprpc::write(buf, v)` | 序列化任意支持的类型 |
| `esprpc::read(r, v)` | 反序列化任意支持的类型 |

---

## 许可证

Apache 2.0
