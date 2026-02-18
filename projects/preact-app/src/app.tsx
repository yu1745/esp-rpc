import { useState, useRef } from 'preact/hooks';
import { UserServiceClient } from './generated/rpc_client';
import { createWebSocketTransport } from './generated/transport-ws';
import type { UserResponse } from './generated/rpc_types';
import './app.css';

const WS_URL = 'ws://192.168.4.1/ws'; // ESP32 默认 SoftAP IP

export function App() {
  const [connected, setConnected] = useState(false);
  const [user, setUser] = useState<UserResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [wsUrl, setWsUrl] = useState(WS_URL);
  const clientRef = useRef<UserServiceClient | null>(null);
  const transportRef = useRef<ReturnType<typeof createWebSocketTransport> | null>(null);

  const connect = async () => {
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

  const disconnect = () => {
    transportRef.current?.disconnect();
    transportRef.current = null;
    clientRef.current = null;
    setConnected(false);
    setUser(null);
  };

  const [loading, setLoading] = useState(false);
  const fetchUser = async () => {
    const client = clientRef.current;
    if (!client) return;
    try {
      setError(null);
      setLoading(true);
      const u = await client.GetUser(1);
      setUser(u);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'RPC failed');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div class="app">
      <h1>ESP RPC Test</h1>
      <div class="card">
        <label>
          WebSocket URL:
          <input
            type="text"
            value={wsUrl}
            onInput={(e) => setWsUrl((e.target as HTMLInputElement).value)}
            disabled={connected}
          />
        </label>
        {!connected ? (
          <button onClick={connect}>Connect</button>
        ) : (
          <button onClick={disconnect}>Disconnect</button>
        )}
      </div>
      {connected && (
        <div class="card">
          <button onClick={fetchUser} disabled={loading}>
            {loading ? '请求中...' : 'GetUser(1)'}
          </button>
        </div>
      )}
      {user && (
        <div class="card result">
          <h3>UserResponse</h3>
          <pre>{JSON.stringify(user, null, 2)}</pre>
        </div>
      )}
      {error && <p class="error">{error}</p>}
    </div>
  );
}
