# ESP-IDF RPC 框架

H5/TypeScript 与 ESP32 之间的 RPC 通信框架，支持单向调用与双向流式数据交换。

## 特性

- **宏定义声明**：通过 `rpc_macros.h` 在 C 头文件中声明 RPC 服务与类型
- **代码生成**：解析 `.rpc.h` 自动生成 TypeScript stub 与类型定义
- **双传输**：底层支持 BLE 与 WebSocket，对上层透明
- **ESP-IDF 组件**：作为组件集成，生成器随 CMake 构建自动执行

## 目录结构

```
esprpc/
├── rpc_macros.h          # 宏定义
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

在 `xxx.rpc.h` 中使用宏定义服务与类型，参考 `projects/esp_test/main/user_service.rpc.h`。

### 2. 生成 TS 代码

```bash
python generator/main.py -o <输出目录> projects/esp_test/main/user_service.rpc.h
```

或通过 CMake 构建 esp_test 时自动生成（输出到 preact-app/src/generated）。

### 3. 配置 TS stub 输出目录

在项目 `CMakeLists.txt` 中：

```cmake
set(ESPRPC_TS_STUB_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../preact-app/src/generated" CACHE PATH "")
```

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
