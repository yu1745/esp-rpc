#!/usr/bin/env npx tsx

import { UserServiceClient } from './rpc_client.js';
import { createWebSocketTransport } from './transport-ws.js';
import { UserStatus } from './rpc_types.js';

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

    // 1. 无参 ListUsers（入参为 undefined 的 Optional）
    console.log('--- ListUsers(undefined) ---');
    const users0 = await client.ListUsers(undefined);
    console.log('用户数:', users0.length);
    for (const u of users0) {
      console.log(`  [${u.id}] name=${u.name} email=${u.email ?? '(无)'} status=${u.status}`);
    }
    console.log('');

    // 2. CreateUser：struct 入参 → struct 返回
    console.log('--- CreateUser() ---');
    const c1 = await client.CreateUser({
      name: 'Alice',
      email: 'alice@test.local',
      password: undefined,
    });
    console.log(`  创建: id=${c1.id} name=${c1.name} email=${c1.email} status=${c1.status}`);
    console.log('');

    // 3. CreateUserV2：VOID 返回（即发即忘，无响应）
    console.log('--- CreateUserV2() (VOID) ---');
    client.CreateUserV2({ name: 'Bob', email: 'bob@test.local', password: 'secret' });
    console.log('  已发送（fire-and-forget，无返回）');
    console.log('');

    // 4. GetUser：单 primitive 入参 → struct 返回
    console.log('--- GetUser() ---');
    const g1 = await client.GetUser(c1.id);
    console.log(`  id=${g1.id} name=${g1.name} email=${g1.email} status=${g1.status}`);
    const gNotFound = await client.GetUser(9999);
    console.log(`  不存在的 id=9999: name=${gNotFound.name}`);
    console.log('');

    // 5. UpdateUser：混合入参 (primitive + struct) → struct 返回
    console.log('--- UpdateUser() ---');
    const u1 = await client.UpdateUser(c1.id, {
      name: 'Alice Updated',
      email: 'alice.new@test.local',
      password: undefined,
    });
    console.log(`  更新后: id=${u1.id} name=${u1.name} email=${u1.email} status=${u1.status}`);
    console.log('');

    // 6. DeleteUser：primitive 入参 → primitive 返回 (bool)
    console.log('--- DeleteUser() ---');
    const delOk = await client.DeleteUser(c1.id);
    console.log(`  删除 id=${c1.id}: ${delOk}`);
    const delAgain = await client.DeleteUser(c1.id);
    console.log(`  再次删除（应失败）: ${delAgain}`);
    console.log('');

    // 7. ListUsers(page: 1)：Optional 有值
    console.log('--- ListUsers(page=1) ---');
    await client.CreateUser({ name: 'Charlie', email: 'charlie@test.local', password: undefined });
    await client.CreateUser({ name: 'Diana', email: 'diana@test.local', password: undefined });
    const users1 = await client.ListUsers(1);
    console.log('用户数:', users1.length);
    for (const u of users1) {
      console.log(`  id=${u.id} name=${u.name} email=${u.email ?? '(无)'} status=${u.status}`);
    }
    console.log('');

    // 8. WatchUsers：流式返回，订阅后主动触发
    console.log('--- WatchUsers() 订阅 3 秒 ---');
    const unsub = client.WatchUsers().subscribe((u) => {
      console.log(`  推送: id=${u.id} name=${u.name} status=${u.status}`);
    });
    await new Promise((r) => setTimeout(r, 3000));
    unsub();
    console.log('  已取消订阅\n');

    // 9. Ping：无参 VOID
    console.log('--- Ping() (VOID) ---');
    client.Ping();
    console.log('  已发送 ping（fire-and-forget，无返回）');
    console.log('');

    // 10. 枚举常量验证
    console.log('--- UserStatus 枚举值 ---');
    console.log(`  ACTIVE=${UserStatus.ACTIVE} INACTIVE=${UserStatus.INACTIVE} DELETED=${UserStatus.DELETED}`);
    console.log('');

    console.log('所有测试完成');
  } catch (err) {
    console.error('错误:', err instanceof Error ? err.message : err);
    process.exit(1);
  } finally {
    transport.disconnect();
  }
}

main();
