import { useState, useRef } from 'preact/hooks';
import { UserServiceClient } from './generated/rpc_client';
import { createWebSocketTransport } from './generated/transport-ws';
import { createBleTransport } from './generated/transport-ble';
import { createSerialTransport } from './generated/transport-serial';
import type { EsprpcTransport } from './generated/transport';
import type { User, CreateUserRequest } from './generated/rpc_types';
import {
  runPerformanceTest,
  type PerformanceTestResult,
  type PerformanceTestProgress,
  formatPerformanceTestResult,
} from './performance-test';
import './app.css';

const WS_URL = 'ws://192.168.4.1/rpc'; // ESP32 默认 SoftAP IP

type TransportMode = 'ws' | 'ble' | 'serial';

const bleSupported = typeof navigator !== 'undefined' && 'bluetooth' in navigator;
const serialSupported =
  typeof navigator !== 'undefined' &&
  !!(navigator as unknown as { serial?: { requestPort: () => Promise<unknown> } }).serial;

export function App() {
  const [transportMode, setTransportMode] = useState<TransportMode>('ws');
  const [connected, setConnected] = useState(false);
  const [result, setResult] = useState<unknown>(null);
  const [streamUsers, setStreamUsers] = useState<User[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [wsUrl, setWsUrl] = useState(WS_URL);
  const [bleDeviceName, setBleDeviceName] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const clientRef = useRef<UserServiceClient | null>(null);
  const transportRef = useRef<EsprpcTransport | null>(null);
  const [watching, setWatching] = useState(false);
  const watchUnsubRef = useRef<(() => void) | null>(null);

  // 性能测试状态
  const [performanceTesting, setPerformanceTesting] = useState(false);
  const [perfTestDuration, setPerfTestDuration] = useState(10);
  const [perfTestConcurrency, setPerfTestConcurrency] = useState(1);
  const [perfTestUserId, setPerfTestUserId] = useState(1);
  const [perfTestResults, setPerfTestResults] = useState<PerformanceTestResult[]>([]);
  const [perfTestProgress, setPerfTestProgress] = useState<PerformanceTestProgress | null>(null);
  const perfAbortControllerRef = useRef<AbortController | null>(null);

  const connectWs = async () => {
    try {
      setError(null);
      const transport = createWebSocketTransport(wsUrl);
      const client = new UserServiceClient(transport);
      await transport.connect();
      transportRef.current = transport;
      clientRef.current = client;
      setConnected(true);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Connection failed');
    }
  };

  const connectBle = async () => {
    if (!bleSupported) {
      setError('Web Bluetooth 不可用，请使用 Chrome/Edge 并确保 HTTPS 或 localhost');
      return;
    }
    try {
      setError(null);
      const transport = createBleTransport();
      const client = new UserServiceClient(transport);
      await transport.connect();
      transportRef.current = transport;
      clientRef.current = client;
      setConnected(true);
      setBleDeviceName('ESPRPC');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'BLE 连接失败');
    }
  };

  const connectSerial = async () => {
    if (!serialSupported) {
      setError('Web Serial API 不可用，请使用 Chrome/Edge 并确保 HTTPS 或 localhost');
      return;
    }
    try {
      setError(null);
      const transport = createSerialTransport({ baudRate: 115200, prefix: 'DRPC', suffix: '\n' });
      const client = new UserServiceClient(transport);
      await transport.connect();
      transportRef.current = transport;
      clientRef.current = client;
      setConnected(true);
    } catch (e) {
      setError(e instanceof Error ? e.message : '串口连接失败');
    }
  };

  const connect = () => {
    if (transportMode === 'ws') connectWs();
    else if (transportMode === 'ble') connectBle();
    else connectSerial();
  };

  const disconnect = () => {
    // 停止性能测试
    perfAbortControllerRef.current?.abort();
    perfAbortControllerRef.current = null;
    setPerformanceTesting(false);
    setPerfTestProgress(null);

    watchUnsubRef.current?.();
    watchUnsubRef.current = null;
    setWatching(false);
    setStreamUsers([]);
    transportRef.current?.disconnect();
    transportRef.current = null;
    clientRef.current = null;
    setConnected(false);
    setResult(null);
    setBleDeviceName(null);
  };

  const startPerformanceTest = async () => {
    const client = clientRef.current;
    if (!client) return;

    setPerformanceTesting(true);
    setPerfTestProgress(null);
    setPerfTestResults([]);

    const abortController = new AbortController();
    perfAbortControllerRef.current = abortController;

    try {
      const result = await runPerformanceTest(client, transportMode, {
        duration: perfTestDuration,
        concurrency: perfTestConcurrency,
        userId: perfTestUserId,
        onProgress: (progress) => {
          setPerfTestProgress(progress);
        },
        signal: abortController.signal,
      });

      setPerfTestResults([result]);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Performance test failed');
    } finally {
      setPerformanceTesting(false);
      setPerfTestProgress(null);
      perfAbortControllerRef.current = null;
    }
  };

  const stopPerformanceTest = () => {
    perfAbortControllerRef.current?.abort();
    perfAbortControllerRef.current = null;
    setPerformanceTesting(false);
    setPerfTestProgress(null);
  };

  const runRpc = async <T,>(fn: () => Promise<T>) => {
    const client = clientRef.current;
    if (!client) return;
    try {
      setError(null);
      setLoading(true);
      const data = await fn();
      setResult(data);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'RPC failed');
    } finally {
      setLoading(false);
    }
  };

  const [getUserId, setGetUserId] = useState(1);
  const [createReq, setCreateReq] = useState<CreateUserRequest>({ name: '', email: '' });
  const [createV2Req, setCreateV2Req] = useState<CreateUserRequest>({ name: '', email: '' });
  const [updateId, setUpdateId] = useState(1);
  const [updateReq, setUpdateReq] = useState<CreateUserRequest>({ name: '', email: '' });
  const [deleteId, setDeleteId] = useState(1);
  const [listPage, setListPage] = useState<number | ''>('');

  return (
    <div class="app">
      <h1>ESP RPC Test</h1>
      <div class="card">
        <div class="transport-tabs">
          <button
            class={transportMode === 'ws' ? 'active' : ''}
            onClick={() => !connected && setTransportMode('ws')}
            disabled={connected}
          >
            WebSocket
          </button>
          <button
            class={transportMode === 'ble' ? 'active' : ''}
            onClick={() => !connected && setTransportMode('ble')}
            disabled={connected}
          >
            Bluetooth
          </button>
          <button
            class={transportMode === 'serial' ? 'active' : ''}
            onClick={() => !connected && setTransportMode('serial')}
            disabled={connected}
          >
            Serial
          </button>
        </div>

        {transportMode === 'ws' && (
          <label class="input-row">
            <span>URL:</span>
            <input
              type="text"
              value={wsUrl}
              onInput={(e) => setWsUrl((e.target as HTMLInputElement).value)}
              disabled={connected}
              placeholder="ws://192.168.4.1/rpc"
            />
          </label>
        )}

        {transportMode === 'ble' && !bleSupported && (
          <p class="ble-hint">Web Bluetooth 需要 Chrome/Edge，且需 HTTPS 或 localhost</p>
        )}

        {transportMode === 'ble' && bleSupported && !connected && (
          <p class="ble-hint">点击连接后，在弹窗中选择 ESPRPC 设备</p>
        )}

        {transportMode === 'serial' && !serialSupported && (
          <p class="ble-hint">Web Serial 需要 Chrome/Edge，且需 HTTPS 或 localhost</p>
        )}

        {transportMode === 'serial' && serialSupported && !connected && (
          <p class="ble-hint">点击连接后，在浏览器弹窗中选择串口设备（如 USB 转串口或 ESP32 串口）</p>
        )}

        {connected && transportMode === 'ble' && bleDeviceName && (
          <p class="connected-label">已连接: {bleDeviceName}</p>
        )}

        {connected && transportMode === 'serial' && (
          <p class="connected-label">已连接: 串口</p>
        )}

        <div class="button-row">
          {!connected ? (
            <button
              onClick={connect}
              disabled={
                (transportMode === 'ble' && !bleSupported) ||
                (transportMode === 'serial' && !serialSupported)
              }
            >
              {transportMode === 'ws'
                ? 'Connect'
                : transportMode === 'ble'
                  ? 'Connect BLE'
                  : 'Connect Serial'}
            </button>
          ) : (
            <button onClick={disconnect}>Disconnect</button>
          )}
        </div>
      </div>

      {connected && (
        <div class="card rpc-methods">
          <h3>RPC 方法</h3>
          <div class="method-grid">
            <div class="method-item">
              <label>GetUser</label>
              <div class="method-inputs">
                <input
                  type="number"
                  value={getUserId}
                  onInput={(e) => setGetUserId(Number((e.target as HTMLInputElement).value) || 0)}
                  placeholder="id"
                  aria-label="GetUser id"
                />
              </div>
              <button onClick={() => runRpc(() => clientRef.current!.GetUser(getUserId))} disabled={loading}>
                GetUser
              </button>
            </div>

            <div class="method-item">
              <label>CreateUser</label>
              <div class="method-inputs">
                <input
                  placeholder="name"
                  value={createReq.name}
                  onInput={(e) => setCreateReq((r) => ({ ...r, name: (e.target as HTMLInputElement).value }))}
                />
                <input
                  placeholder="email"
                  value={createReq.email}
                  onInput={(e) => setCreateReq((r) => ({ ...r, email: (e.target as HTMLInputElement).value }))}
                />
              </div>
              <button
                onClick={() => runRpc(() => clientRef.current!.CreateUser(createReq))}
                disabled={loading || !createReq.name || !createReq.email}
              >
                CreateUser
              </button>
            </div>

            <div class="method-item">
              <label>CreateUserV2 (void)</label>
              <div class="method-inputs">
                <input
                  placeholder="name"
                  value={createV2Req.name}
                  onInput={(e) => setCreateV2Req((r) => ({ ...r, name: (e.target as HTMLInputElement).value }))}
                />
                <input
                  placeholder="email"
                  value={createV2Req.email}
                  onInput={(e) => setCreateV2Req((r) => ({ ...r, email: (e.target as HTMLInputElement).value }))}
                />
              </div>
              <button
                onClick={() => {
                  const client = clientRef.current;
                  if (!client) return;
                  try {
                    setError(null);
                    client.CreateUserV2(createV2Req);
                    setResult({ message: 'CreateUserV2 sent (fire-and-forget)' });
                  } catch (e) {
                    setError(e instanceof Error ? e.message : 'RPC failed');
                  }
                }}
                disabled={!createV2Req.name || !createV2Req.email}
              >
                CreateUserV2
              </button>
            </div>

            <div class="method-item">
              <label>UpdateUser</label>
              <div class="method-inputs">
                <input
                  type="number"
                  placeholder="id"
                  value={updateId}
                  onInput={(e) => setUpdateId(Number((e.target as HTMLInputElement).value) || 0)}
                />
                <input
                  placeholder="name"
                  value={updateReq.name}
                  onInput={(e) => setUpdateReq((r) => ({ ...r, name: (e.target as HTMLInputElement).value }))}
                />
                <input
                  placeholder="email"
                  value={updateReq.email}
                  onInput={(e) => setUpdateReq((r) => ({ ...r, email: (e.target as HTMLInputElement).value }))}
                />
              </div>
              <button
                onClick={() => runRpc(() => clientRef.current!.UpdateUser(updateId, updateReq))}
                disabled={loading}
              >
                UpdateUser
              </button>
            </div>

            <div class="method-item">
              <label>DeleteUser</label>
              <div class="method-inputs">
                <input
                  type="number"
                  value={deleteId}
                  onInput={(e) => setDeleteId(Number((e.target as HTMLInputElement).value) || 0)}
                  placeholder="id"
                  aria-label="DeleteUser id"
                />
              </div>
              <button onClick={() => runRpc(() => clientRef.current!.DeleteUser(deleteId))} disabled={loading}>
                DeleteUser
              </button>
            </div>

            <div class="method-item">
              <label>ListUsers</label>
              <div class="method-inputs">
                <input
                  type="number"
                  placeholder="page (可选)"
                  value={listPage}
                  onInput={(e) => {
                    const v = (e.target as HTMLInputElement).value;
                    setListPage(v === '' ? '' : Number(v) || 0);
                  }}
                />
              </div>
              <button
                onClick={() => runRpc(() => clientRef.current!.ListUsers(listPage === '' ? undefined : listPage))}
                disabled={loading}
              >
                ListUsers
              </button>
            </div>

            <div class="method-item">
              <label>WatchUsers (流式)</label>
              <div class="method-inputs" />
              {watching ? (
                <button
                  onClick={() => {
                    watchUnsubRef.current?.();
                    watchUnsubRef.current = null;
                    setWatching(false);
                    setStreamUsers([]);
                  }}
                >
                  停止订阅
                </button>
              ) : (
                <button
                  onClick={() => {
                    const client = clientRef.current;
                    if (!client) return;
                    const { subscribe } = client.WatchUsers();
                    watchUnsubRef.current = subscribe((u) => {
                      setStreamUsers((prev) => [...prev, u]);
                    });
                    setWatching(true);
                  }}
                  disabled={loading}
                >
                  订阅
                </button>
              )}
            </div>
          </div>
        </div>
      )}

      {connected && !performanceTesting && (
        <div class="card perf-test">
          <h3>性能测试 - GetUser</h3>
          <div class="perf-config">
            <label class="input-row">
              <span>时长(秒):</span>
              <input
                type="number"
                min="1"
                max="300"
                value={perfTestDuration}
                onInput={(e) => setPerfTestDuration(Number((e.target as HTMLInputElement).value) || 10)}
              />
            </label>
            <label class="input-row">
              <span>并发数:</span>
              <input
                type="number"
                min="1"
                max="100"
                value={perfTestConcurrency}
                onInput={(e) => setPerfTestConcurrency(Number((e.target as HTMLInputElement).value) || 1)}
              />
            </label>
            <label class="input-row">
              <span>用户ID:</span>
              <input
                type="number"
                min="1"
                value={perfTestUserId}
                onInput={(e) => setPerfTestUserId(Number((e.target as HTMLInputElement).value) || 1)}
              />
            </label>
          </div>
          <button onClick={startPerformanceTest} disabled={performanceTesting || loading}>
            开始性能测试 ({transportMode.toUpperCase()})
          </button>
        </div>
      )}

      {performanceTesting && (
        <div class="card perf-test-progress">
          <h3>性能测试进行中...</h3>
          {perfTestProgress && (
            <div class="progress-stats">
              <p>已发送请求: {perfTestProgress.currentRequest}</p>
              <p>成功: {perfTestProgress.successfulRequests} | 失败: {perfTestProgress.failedRequests}</p>
              <p>已用时间: {(perfTestProgress.elapsed / 1000).toFixed(2)} 秒</p>
              <p>当前速率: {perfTestProgress.currentRequestsPerSecond.toFixed(2)} req/s</p>
            </div>
          )}
          <button onClick={stopPerformanceTest}>停止测试</button>
        </div>
      )}

      {perfTestResults.length > 0 && (
        <div class="card perf-result">
          <h3>性能测试结果</h3>
          {perfTestResults.map((result, idx) => (
            <div key={idx} class="result-item">
              <pre>{formatPerformanceTestResult(result)}</pre>
            </div>
          ))}
        </div>
      )}

      {loading && <p class="loading">请求中...</p>}

      {result !== null && (
        <div class="card result">
          <h3>响应</h3>
          <pre>{JSON.stringify(result, null, 2)}</pre>
        </div>
      )}

      {streamUsers.length > 0 && (
        <div class="card result">
          <h3>WatchUsers 流式数据 ({streamUsers.length})</h3>
          <pre>{JSON.stringify(streamUsers, null, 2)}</pre>
        </div>
      )}

      {error && <p class="error">{error}</p>}
    </div>
  );
}
