# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

这是一个 ESP-IDF 测试项目，用于演示 esp-rpc 框架 - 一个 H5/TypeScript 客户端与 ESP32 之间的 RPC 通信框架。项目同时支持 WebSocket 和 BLE（蓝牙低功耗）双传输方式。

## 构建命令

```bash
# 设置目标芯片（首次或更换芯片时）
idf.py set-target esp32c3

# 构建项目
idf.py build

# 烧录到设备
idf.py flash

# 监控串口输出
idf.py monitor
```

## 必需配置

**构建前必须配置 WiFi 凭据：**

将 `main/wifi_config_local.h.example` 复制为 `main/wifi_config_local.h` 并填入你的 WiFi SSID 和密码。

```c
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASSWORD  "YOUR_PASSWORD"
```

此文件已被 gitignore，不会提交到仓库。

## 架构说明

### RPC 框架布局

esp-rpc 组件位于 `../../`（`projects/` 的父目录）：
- `include/esprpc.h` - C API
- `include/esprpc.hpp` - C++ 模板序列化 + 宏定义
- `include/esprpc_binary.h` - 二进制协议序列化 API
- `include/esprpc_service.h` - 服务注册 API
- `include/esprpc_transport.h` - 传输层抽象
- `src/` - C 实现（esprpc.c, esprpc_binary.c, transport 层）

### 服务定义方式（C++ 模板 + 宏）

无需代码生成器，使用 `esprpc.hpp` 中的宏直接定义：

1. **数据结构**：使用 `ESPRPC_STRUCT` 宏定义 struct 并自动生成序列化
2. **服务类**：编写普通 C++ 类，方法作为 RPC 处理函数
3. **服务注册**：使用 `ESPRPC_SERVICE` 宏注册方法列表，编译期自动生成 dispatch 函数

示例见 `main/user_service.hpp`，注册方式为在 `main.cpp` 中调用 `UserService_register_rpc()`。

### RPC 类型系统

- **基础类型**：`int32_t`、`uint32_t`、`int16_t`、`uint16_t`、`uint8_t`、`bool`
- **字符串**：`esprpc::StringBuf`（固定 128 字节）
- **可选值**：`esprpc::Optional<T>`
- **列表**：`esprpc::List<T>`
- **流式返回**：`rpc_stream<T>`
- **自定义结构体**：通过 `ESPRPC_STRUCT` 或 `ESPRPC_SERIALIZE` 宏定义

### 二进制协议（帧格式）

```
[1B method_id][2B invoke_id LE][2B payload_len LE][binary payload]
```

编码规则：
- int32/uint32: 4 字节小端
- bool: 1 字节
- string: [2B 长度 LE][UTF-8 字节]
- Optional: [1B present 标记][值（如存在）]
- List: [4B 数量 LE][值序列]

### 服务注册

在 `main.cpp` 中调用宏生成的注册函数：
```cpp
UserService_register_rpc();
```

宏展开后内部调用 `esprpc_register_service_ex("UserService", &impl_instance, dispatch_fn)`。

### 传输层

- **WebSocket**：`transport_http_ws.c` 处理 HTTP 服务器升级到 WebSocket
  - 端点：`ws://<设备-ip>/ws`（端口 80）
  - 通过 `CONFIG_HTTPD_WS_SUPPORT` 启用
- **BLE**：`transport_ble.c` 实现 NimBLE GATT 服务器
  - 通过 `CONFIG_ESPRPC_ENABLE_BLE` 启用
- **串口**：`transport_serial.c` 外部 UART 传输
  - 通过 `CONFIG_ESPRPC_ENABLE_SERIAL` 启用
- 所有传输使用统一的接收回调 `transport_recv_to_rpc()`

## 文件组织

```
main/
├── main.cpp                   # app_main、WiFi 初始化、RPC 注册
├── wifi_sta.cpp/hpp           # WiFi 事件处理器
├── wifi_config_local.h        # WiFi 凭据（不在 git 中，从示例创建）
└── user_service.hpp           # RPC 服务定义与实现（使用 ESPRPC 宏）
```

## 添加新的 RPC 服务

1. 创建 `main/new_service.hpp`，包含服务类定义
2. 使用 `ESPRPC_STRUCT` 宏定义数据结构
3. 使用 `ESPRPC_SERVICE` 宏注册方法列表
4. 在 `main.cpp` 中添加 `#include "new_service.hpp"` 并调用 `NewService_register_rpc()`
5. 构建项目

## SDK 配置

`sdkconfig.defaults` 中的关键设置：

### 传输层配置
- `CONFIG_HTTPD_WS_SUPPORT=y` - 启用 WebSocket
- `CONFIG_BT_ENABLED=y` - 启用蓝牙
- `CONFIG_BT_NIMBLE_ENABLED=y` - 使用 NimBLE
- `CONFIG_BT_CONTROLLER_ENABLED=y` - 启用蓝牙控制器
- `CONFIG_ESPRPC_ENABLE_BLE` - 在 `idf.py menuconfig` 的 `Component config → ESPRPC` 中配置
- `CONFIG_ESPRPC_ENABLE_SERIAL` - 启用串口传输

### 传输方式选择

| 传输方式 | 适用场景 | 优点 | 缺点 |
|---------|---------|------|------|
| **WebSocket** | 已有 WiFi 环境，高吞吐量 | 速度快、延迟低 | 需要 WiFi 网络 |
| **BLE** | 移动设备、无网络环境 | 功耗低、无需网络 | 带宽有限 |
| **串口** | 有线连接、调试 | 简单可靠 | 需物理连接 |
