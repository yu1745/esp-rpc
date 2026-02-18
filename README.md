# ESP-IDF RPC 框架

H5/TypeScript 与 ESP32 之间的 RPC 通信框架，支持单向调用与双向流式数据交换。

## 特性

- **宏定义声明**：通过 `rpc_macros.hpp` 在头文件中声明 RPC 服务与类型
- **代码生成**：解析 `.rpc.hpp` 自动生成 TypeScript stub 与类型定义
- **多传输**：底层支持 WebSocket、BLE 与串口（Serial），对上层透明
- **ESP-IDF 组件**：作为组件集成，生成器随 CMake 构建自动执行

## 目录结构

```
esprpc/
├── rpc_macros.hpp        # 宏定义
├── generator/             # Python 代码生成器
├── include/               # ESP 头文件
├── src/                   # ESP 实现
├── ts/                    # TypeScript 客户端库（transport 源在 ts/src）
│   ├── src/               # transport 实现（generator 从此复制）
│   └── generated/         # 生成产物（可删除，由 gen 重建）
└── projects/
    ├── esp_test/          # ESP-IDF 测试工程
    ├── preact-app/        # Preact + Vite 前端工程
    └── ts_test/           # 纯 TS 测试（Node.js + tsx，无浏览器）
```

## 快速开始

### 1. 定义 RPC 服务

在 `xxx.rpc.hpp` 中使用宏定义服务与类型，参考 `projects/esp_test/main/user_service.rpc.hpp`。

### 2. 生成 TS 代码

```bash
python generator/main.py -o <输出目录> projects/esp_test/main/user_service.rpc.hpp
```

或通过 CMake 构建 esp_test 时自动生成（输出到 preact-app/src/generated）。

### 3. 配置 TS stub 输出目录

在 `idf.py menuconfig` → **Component config → ESP RPC Configuration** 中设置 **TS stub output path**，或在项目根目录的 `sdkconfig.defaults` 中设置：

```
CONFIG_ESPRPC_TS_STUB_OUTPUT_DIR="../preact-app/src/generated"
```

留空则不生成 TypeScript stub（仅生成 C++ 代码）。路径可为相对项目根或绝对路径。

### 4. 使用生成的客户端

```typescript
import { UserServiceClient } from './generated/rpc_client';
import { createWebSocketTransport } from './generated/transport-ws';

const transport = createWebSocketTransport('ws://192.168.1.100/rpc');
const userService = new UserServiceClient(transport);
await transport.connect();

const user = await userService.GetUser(1);
userService.WatchUsers().subscribe((u) => console.log(u));
```

## 串口（Serial）传输层

串口传输与 WebSocket/BLE 使用相同二进制帧格式：`[1B method_id][2B invoke_id LE][2B payload_len LE][payload]`，可选在每帧前后配置**前缀（prefix）**和**后缀（suffix）**，便于与其他协议复用同一串口。

### 稳定性说明

串口传输**天生不如 WebSocket/BLE 稳定**，主要原因：

- **难以独占串口**：串口常被日志、调试、其他协议共用，无法像 TCP/WebSocket 那样由单一连接独占通道，易发生帧与其它数据交织、误解析。
- **建议**：
  1. **强烈建议为 RPC 帧配置不易重复的前缀与后缀**（如使用 `\xNN` 形式的若干字节，或较长、带随机/魔数的字面量），以便在混合数据流中可靠识别 RPC 边界，减少误匹配。
  2. **当应用层（外部代码）控制串口时，RPC 在写串口时必须独占串口**：在 `esprpc_serial_set_tx_cb()` 提供的发送回调里，应保证**整包（prefix + 帧 + suffix）原子写入**，写完成前不要让其他逻辑往同一串口写入，否则易造成写入被截断、半包发送，导致对端解析失败或状态错乱。

### 配置与使用

- **ESP 端**：在 `idf.py menuconfig` → **Component config → ESP RPC Configuration** 中启用 **Enable serial transport**，并设置 **Optional packet prefix** / **Optional packet suffix**（支持字面量或 `\xNN`，最多 16 字节）。串口由应用管理，需调用 `esprpc_serial_set_tx_cb()` 注册发送回调，并在串口收到数据后根据前后缀识别 RPC 包，通过 `esprpc_serial_feed_packet()` 或 `esprpc_serial_feed_raw_packet()` 喂给框架。
- **TS 端**：使用生成的 `createSerialTransport({ prefix, suffix, baudRate })`（如 Web Serial API），与 ESP 端前后缀保持一致即可。

## 测试工程

### ESP-IDF 工程

```bash
cd projects/esp_test
idf.py set-target esp32
idf.py build
```

### Preact 前端

```bash
cd projects/preact-app
pnpm install
pnpm dev
```

生成 RPC 代码：`pnpm gen`（或构建 esp_test 时自动生成）

### 纯 TS 测试（Node.js）

```bash
cd projects/ts_test
pnpm install
pnpm gen
pnpm start
```

无浏览器依赖，使用 tsx 直接连接 ESP32 WebSocket 测试 RPC。需 Node.js 22+（内置 WebSocket）。

## 依赖

- ESP-IDF 5.x
- Python 3
- pnpm
- TypeScript 5.x（TS 端）
