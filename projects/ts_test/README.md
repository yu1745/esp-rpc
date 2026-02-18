# ts_test - 纯 TypeScript RPC 测试

无浏览器依赖，使用 tsx 在 Node.js 中运行，直接测试 RPC 客户端与 ESP32 的通信。复用前端 `transport-ws`，依赖 Node.js 22+ 内置 WebSocket。

## 前置条件

- **Node.js 22+**（内置 WebSocket）
- ESP32 设备已烧录 esp_test 并运行（WebSocket 在 `ws://<ip>:80/ws`）

## 安装

```bash
cd projects/ts_test
pnpm install
```

## 生成 RPC 代码

```bash
pnpm gen
```

## 运行

```bash
pnpm start
```

或指定 WebSocket 地址：

```bash
pnpm start ws://192.168.4.1/ws
# 或通过环境变量
ESPRPC_WS_URL=ws://192.168.1.100/ws pnpm start
```

## 测试内容

- `ListUsers` - 获取用户列表
- `CreateUser` - 创建用户
- `GetUser` - 根据 ID 获取用户
- `WatchUsers` - 订阅流式推送（3 秒后取消）
