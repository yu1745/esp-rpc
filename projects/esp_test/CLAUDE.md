# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

这是一个 ESP-IDF 测试项目，用于演示 esp-rpc 框架 - 一个 H5/TypeScript 客户端与 ESP32 之间的 RPC 通信框架。项目同时支持 WebSocket 和 BLE（蓝牙低功耗）双传输方式，包含 ESP32 (C++) 和客户端 (TypeScript) 的代码生成。

## 构建命令

```bash
# 设置目标芯片（首次或更换芯片时）
idf.py set-target esp32

# 构建项目（同时触发 RPC 代码生成）
idf.py build

# 烧录到设备
idf.py flash

# 监控串口输出
idf.py monitor

# 烧录并监控（合并命令）
idf.py flash monitor
```

## 必需配置

**构建前必须配置 WiFi 凭据：**

将 `main/wifi_config_local.h.example` 复制为 `main/wifi_config_local.h` 并填入你的 WiFi SSID 和密码。如果缺少此文件，构建会失败并显示清晰的错误信息。

```c
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASSWORD  "YOUR_PASSWORD"
```

此文件已被 gitignore，不会提交到仓库。

## 架构说明

### RPC 框架布局

esp-rpc 组件位于 `../../`（`projects/` 的父目录）：
- `include/` - 核心 RPC API 的 C 头文件
- `src/` - C 实现（esprpc.c, esprpc_binary.c, transport 层）
- `generator/` - Python 代码生成器
- `rpc_macros.hpp` - 在 `.rpc.hpp` 文件中定义 RPC 服务的宏

### 代码生成流程

1. **在 `.rpc.hpp` 文件中定义 RPC 服务**，使用 `rpc_macros.hpp` 中的宏
   - 示例：`main/user_service.rpc.hpp`
   - 使用：`RPC_SERVICE`、`RPC_METHOD`、`RPC_STRUCT`、`RPC_ENUM` 等

2. **CMake 构建触发代码生成**（见 `../../CMakeLists.txt:22-34`）
   - 扫描所有 `*.rpc.hpp` 文件
   - 构建时运行 `generator/main.py`
   - 将 TypeScript stub 输出到 `CONFIG_ESPRPC_TS_STUB_OUTPUT_DIR`（在 menuconfig 或 `sdkconfig.defaults` 中配置）

3. **生成的 C++ 文件**（必须添加到 `main/CMakeLists.txt` 的 SRCS）：
   - `<service>.rpc.gen.hpp` - 分发函数声明 + 服务实例 extern（自动生成）
   - `<service>.rpc.gen.cpp` - 分发/序列化代码（自动生成，**请勿编辑**）

4. **用户实现**在 `<service>.rpc.impl_user.cpp` 中：
   - 此文件**不会被**生成器覆盖
   - 包含实际业务逻辑（如 `user_service.rpc.impl_user.cpp`）
   - 命名为 `<method>_impl()` 的函数会被分发层调用

### RPC 类型系统（纯 C++ 模板）

- **基础类型**：`int`、`bool`、`string` (char*)
- **类型修饰符**：`REQUIRED(type)`、`OPTIONAL(type)`、`LIST(type)`、`STREAM(type)`
- **修饰符展开**为 C++ 模板（见 `rpc_macros.hpp`）
  - `OPTIONAL(T)` → `rpc_optional<T>`
  - `LIST(T)` → `rpc_list<T>`
  - `STREAM(T)` → `rpc_stream<T>`，包含 `ctx` 指针

### 服务注册

在 `main.cpp` 中：
```c
esprpc_register_service_ex("UserService", &user_service_impl_instance, UserService_dispatch);
```

服务实例在 gen.cpp 中定义，实现函数在 `user_service.rpc.impl_user.cpp` 中编写。

### 传输层

项目支持两种传输方式，对上层 RPC 调用透明：

- **WebSocket 传输**：`transport_http_ws.c` 处理 HTTP 服务器升级到 WebSocket
  - 服务器在 WiFi 获取 IP 后启动（见 `wifi_sta.c:79-86`）
  - 端点：`ws://<设备-ip>/ws`（端口 80）
  - 通过 `CONFIG_HTTPD_WS_SUPPORT` 启用/禁用

- **BLE 传输**：`transport_ble.c` 实现 NimBLE GATT 服务器
  - 使用 Bluetooth Low Energy 进行 RPC 通信
  - 启动后自动广播，无需网络连接
  - 通过 `CONFIG_ESPRPC_ENABLE_BLE` 启用/禁用（需要 `BT_ENABLED` 和 `BT_NIMBLE_ENABLED`）

- **共享的接收回调**：两种传输使用统一的 `transport_recv_to_rpc()` 回调（见 `main.c:30-34`）
- **二进制协议**：在 `esprpc_binary.c` 中处理序列化/反序列化

## 文件组织

```
main/
├── main.cpp                   # app_main、WiFi 初始化、RPC 注册
├── wifi_sta.c/h              # WiFi 事件处理器
├── wifi_config_local.h       # WiFi 凭据（不在 git 中，从示例创建）
├── user_service.rpc.hpp      # RPC 服务定义（编辑此文件）
├── user_service.rpc.gen.hpp  # 生成的头文件（请勿编辑）
├── user_service.rpc.gen.cpp   # 生成的分发代码（请勿编辑）
└── user_service.rpc.impl_user.cpp # 用户实现（编辑此文件，不会被覆盖）
```

## 添加新的 RPC 服务

1. 创建 `main/new_service.rpc.hpp`，使用 RPC 宏定义服务
2. 在 `main/CMakeLists.txt` 的 SRCS 中添加：`new_service.rpc.gen.cpp`、`new_service.rpc.impl_user.cpp`
3. 在 `new_service.rpc.impl_user.cpp` 中实现方法（从生成器输出创建）
4. 在 `main.c` 中注册：`esprpc_register_service_ex("NewService", &new_service_impl_instance, NewService_dispatch)`
5. 构建项目 - 生成器将创建 `.rpc.gen.hpp`、`.rpc.gen.cpp` 和 TS stubs

## SDK 配置

`sdkconfig.defaults` 中的关键设置：

### 传输层配置
- `CONFIG_HTTPD_WS_SUPPORT=y` - 启用 WebSocket 传输支持
- `CONFIG_BT_ENABLED=y` - 启用蓝牙
- `CONFIG_BT_NIMBLE_ENABLED=y` - 使用 NimBLE 协议栈
- `CONFIG_BT_CONTROLLER_ENABLED=y` - 启用蓝牙控制器

### Kconfig 配置选项
- `CONFIG_ESPRPC_ENABLE_BLE` - 在 `idf.py menuconfig` 的 `Component config → ESPRPC` 中配置
  - 默认值：`y`（启用）
  - 依赖：`BT_ENABLED` && `BT_NIMBLE_ENABLED`
  - 用于启用/禁用 BLE 传输

### 传输方式选择

两种传输可以**同时工作**，根据使用场景选择：

| 传输方式 | 适用场景 | 优点 | 缺点 |
|---------|---------|------|------|
| **WebSocket** | 已有 WiFi 环境，高吞吐量 | 速度快、延迟低、支持大数据传输 | 需要 WiFi 网络 |
| **BLE** | 移动设备、无网络环境 | 功耗低、无需网络、配对后自动连接 | 带宽有限、MTU 较小 |
