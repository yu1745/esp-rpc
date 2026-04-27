# ESP-IDF RPC 框架

H5/TypeScript 与 ESP32 之间的 RPC 通信框架，支持单向调用与流式返回。

## 特性

- **C++ 模板序列化**：通过 `esprpc.hpp` 中的宏 (`ESPRPC_STRUCT`, `ESPRPC_SERIALIZE`, `ESPRPC_SERVICE`) 在头文件中声明 RPC 服务与类型，编译期自动生成序列化代码与分发函数，无需额外代码生成器
- **多传输**：底层支持 WebSocket、BLE 与串口（Serial），对上层透明
- **ESP-IDF 组件**：作为组件集成，通过 `idf.py menuconfig` 配置

## 目录结构

```
esprpc/
├── include/
│   ├── esprpc.h              # C API
│   ├── esprpc.hpp            # C++ 模板序列化 + 宏
│   ├── esprpc_binary.h       # 二进制协议序列化 API
│   ├── esprpc_service.h      # 服务注册 API
│   └── esprpc_transport.h    # 传输层抽象
├── src/                      # C 实现
│   ├── esprpc.c              # 核心：服务注册、请求分发、内存池
│   ├── esprpc_binary.c       # 二进制读写原语
│   ├── transport_http_ws.c   # WebSocket 传输
│   ├── transport_ble.c       # BLE NimBLE 传输
│   └── transport_serial.c    # 串口传输
├── examples/helloworld/      # 最小示例
├── projects/
│   ├── esp_test/             # ESP-IDF 测试工程
│   ├── preact-app/           # Preact + Vite 前端工程
│   └── ts_test/              # 纯 TS 测试（Node.js + tsx）
└── ts/                       # TypeScript 客户端库
```

## 快速开始

### 1. 定义 RPC 服务

在 C++ 头文件中使用 `ESPRPC_SERVICE` 宏定义服务，参考 `projects/esp_test/main/user_service.hpp`。使用 `ESPRPC_STRUCT` 或 `ESPRPC_SERIALIZE` 宏为结构体添加自动序列化支持。

### 2. 构建固件

```bash
cd projects/esp_test
idf.py set-target esp32c3
idf.py menuconfig    # 配置传输层（WebSocket/BLE/Serial）
idf.py build
```

### 3. 使用生成的客户端

TypeScript 客户端手写，使用与 C++ 端一致的二进制帧格式通信：

```typescript
import { UserServiceClient } from './generated/rpc_client';
import { createWebSocketTransport } from './generated/transport-ws';

const transport = createWebSocketTransport('ws://192.168.4.1/rpc');
const userService = new UserServiceClient(transport);
await transport.connect();

const user = await userService.GetUser(1);
userService.WatchUsers().subscribe((u) => console.log(u));
```

## 二进制协议

帧格式：
```
[1B method_id][2B invoke_id LE][2B payload_len LE][N bytes binary payload]
```

- `invoke_id`: 0 = 流式推送, 非 0 = 请求-响应匹配
- `method_id`: 高 3 位 = 服务索引, 低 5 位 = 方法索引

负载编码：
- `int32_t`/`uint32_t`: 4 字节小端
- `bool`: 1 字节
- `string`: [2B 长度 LE][UTF-8 字节]
- `Optional<T>`: [1B present 标记][T 值（如果存在）]
- `List<T>`: [4B 数量 LE][T 值序列]

## 返回类型说明

### VOID 返回类型（即发即忘）

使用 `MF_VOID` 标记的方法采用"即发即忘"（fire-and-forget）模式：

```cpp
RPC_METHOD(CreateUserV2, VOID, CreateUserRequest request)
```

- 客户端发送请求后**立即返回**，不等待服务端响应
- 适用于日志记录、事件通知等不关心结果的场景

### 流式返回

使用 `rpc_stream<T>` 返回类型的方法支持服务端持续推送：

```cpp
RPC_METHOD(WatchUsers, STREAM(User), void)
```

- 客户端通过 `subscribe` 接收推送数据
- 服务端调用 `esprpc::stream_emit(method_id, data)` 发送

## 传输层概览

### WebSocket

支持复用已有的 `httpd` 服务器，也可由 esp-rpc 自行创建。

### BLE

目前独占整个蓝牙栈。计划在后续版本中提供回调接口，允许用户注册自定义 BLE service。

### 串口（Serial）

使用相同二进制帧格式，可选配置前缀/后缀用于帧边界识别。

## 测试工程

### ESP-IDF 工程

```bash
cd projects/esp_test
idf.py set-target esp32c3
idf.py build
```

### Preact 前端

```bash
cd projects/preact-app
pnpm install
pnpm dev
```

### 纯 TS 测试（Node.js）

```bash
cd projects/ts_test
pnpm install
pnpm start
```

无浏览器依赖，使用 tsx 直接连接 ESP32 WebSocket 测试 RPC。需 Node.js 22+（内置 WebSocket）。

## 依赖

- ESP-IDF 5.x
- pnpm
- TypeScript 5.x（TS 端）
