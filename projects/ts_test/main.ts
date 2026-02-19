#!/usr/bin/env npx tsx
/**
 * 纯 TypeScript RPC 测试（Node.js + tsx，无浏览器）
 * 用法: pnpm start [ws_url]
 * 默认: ws://192.168.4.1/rpc (ESP32 SoftAP)
 */

import { UserServiceClient } from './generated/rpc_client.js';
import { createWebSocketTransport } from './generated/transport-ws.js';

const WS_URL = process.argv[2] ?? process.env.ESPRPC_WS_URL ?? 'ws://192.168.4.1/rpc';

async function main() {
  console.log('ESP-RPC 测试 (Node.js)');
  console.log('连接:', WS_URL);
  console.log('');

  const transport = createWebSocketTransport(WS_URL);
  const client = new UserServiceClient(transport);

  try {
    await transport.connect();
    console.log('已连接\n');
    /* 短暂延迟，确保 ESP WebSocket 握手完全就绪后再发首帧 */
    await new Promise((r) => setTimeout(r, 100));

    // 1. ListUsers（C 端可能返回 []、{items, len} 或 {}）
    console.log('--- ListUsers() ---');
    const raw = await client.ListUsers(undefined);
    const users: unknown[] = Array.isArray(raw) ? raw : (raw && typeof raw === 'object' && Array.isArray((raw as { items?: unknown[] }).items) ? (raw as { items: unknown[] }).items : []);
    console.log('用户数:', users.length);
    for (let i = 0; i < users.length; i++) {
      const u = users[i] as { id?: number; name?: string; status?: number };
      console.log(`  [${i}] id=${u.id} name=${u.name} status=${u.status}`);
    }
    console.log('');

    // 2. CreateUser（C 端占位实现可能返回 {}）
    console.log('--- CreateUser() ---');
    const created = await client.CreateUser({
      name: 'ts_test_user',
      email: 'ts@test.local',
    });
    console.log(created?.id != null ? `创建用户: id=${created.id} name=${created.name}` : '创建用户: （C 端占位，返回空）');
    console.log('');

    // 3. GetUser
    console.log('--- GetUser() ---');
    const userId = created?.id ?? 1;
    const got = await client.GetUser(userId);
    console.log(got?.id != null ? `获取用户: id=${got.id} name=${got.name} email=${got.email}` : '获取用户: （C 端占位，返回空）');
    console.log('');

    // 4. WatchUsers 订阅（3 秒后取消）
    console.log('--- WatchUsers() 订阅 3 秒 ---');
    const unsub = client.WatchUsers().subscribe((u: { id?: number; name?: string }) => {
      console.log('  流式推送:', u?.id, u?.name);
    });
    await new Promise((r) => setTimeout(r, 3000));
    unsub();
    console.log('已取消订阅\n');

    console.log('测试完成');
  } catch (err) {
    console.error('错误:', err instanceof Error ? err.message : err);
    process.exit(1);
  } finally {
    transport.disconnect();
  }
}

main();
