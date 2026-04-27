#!/usr/bin/env npx tsx
/**
 * 纯 TypeScript RPC 测试（Node.js + tsx，无浏览器）
 * 用法: pnpm start [ws_url]
 * 默认: ws://192.168.4.1/rpc (ESP32 SoftAP)
 */

import { UserServiceClient } from './rpc_client.js';
import { createWebSocketTransport } from './transport-ws.js';

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
    await new Promise((r) => setTimeout(r, 100));

    console.log('--- ListUsers() ---');
    const users = await client.ListUsers(undefined);
    console.log('用户数:', users.length);
    for (let i = 0; i < users.length; i++) {
      console.log(`  [${i}] id=${users[i].id} name=${users[i].name} status=${users[i].status}`);
    }
    console.log('');

    console.log('--- CreateUser() ---');
    const created = await client.CreateUser({
      name: 'ts_test_user',
      email: 'ts@test.local',
    });
    console.log(`创建用户: id=${created.id} name=${created.name}`);
    console.log('');

    console.log('--- GetUser() ---');
    const userId = created?.id ?? 1;
    const got = await client.GetUser(userId);
    console.log(`获取用户: id=${got.id} name=${got.name} email=${got.email}`);
    console.log('');

    console.log('--- WatchUsers() 订阅 3 秒 ---');
    const unsub = client.WatchUsers().subscribe((u) => {
      console.log('  流式推送:', u.id, u.name);
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
