import type { EsprpcTransport } from './transport';

function closeCodeMessage(code: number): string {
  const map: Record<number, string> = {
    1000: '正常关闭',
    1001: '端点离开',
    1002: '协议错误',
    1003: '不支持的数据类型',
    1006: '异常关闭（连接被拒绝、网络不可达或 DNS 解析失败）',
    1007: '无效数据',
    1008: '策略违规',
    1009: '消息过大',
    1010: '需要扩展',
    1011: '服务器内部错误',
    1015: 'TLS 握手失败',
  };
  return map[code] ?? `未知错误`;
}

function encodeFrame(methodId: number, invokeId: number, payload: Uint8Array): ArrayBuffer {
  const frame = new Uint8Array(5 + payload.length);
  frame[0] = methodId;
  frame[1] = invokeId & 0xff;
  frame[2] = (invokeId >> 8) & 0xff;
  frame[3] = payload.length & 0xff;
  frame[4] = (payload.length >> 8) & 0xff;
  frame.set(payload, 5);
  return frame.buffer;
}

export function createWebSocketTransport(url: string): EsprpcTransport {
  let ws: WebSocket | null = null;
  let nextInvokeId = 1;
  const pending = new Map<number, { resolve: (v: Uint8Array) => void; reject: (e: Error) => void }>();
  const streamSubs = new Map<number, (data: Uint8Array) => void>();

  return {
    async call(methodId: number, payload: Uint8Array, options?: { timeout?: number }): Promise<Uint8Array> {
      return new Promise((resolve, reject) => {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          reject(new Error('Not connected'));
          return;
        }
        const invokeId = nextInvokeId++;
        const timeoutMs = options?.timeout ?? 10000;
        const t = setTimeout(() => {
          if (pending.delete(invokeId)) {
            reject(new Error(`RPC 超时 (${timeoutMs}ms)`));
          }
        }, timeoutMs);
        pending.set(invokeId, {
          resolve: (v) => {
            clearTimeout(t);
            resolve(v);
          },
          reject: (e) => {
            clearTimeout(t);
            reject(e);
          },
        });
        ws.send(encodeFrame(methodId, invokeId, payload));
      });
    },
    subscribe(methodId: number, cb: (data: Uint8Array) => void): void {
      streamSubs.set(methodId, cb);
    },
    unsubscribe(methodId: number): void {
      streamSubs.delete(methodId);
    },
    async connect(): Promise<void> {
      ws = new WebSocket(url);
      await new Promise<void>((resolve, reject) => {
        if (!ws) return reject(new Error('No socket'));
        let settled = false;
        const settle = (err?: Error) => {
          if (settled) return;
          settled = true;
          if (err) reject(err);
          else resolve();
        };
        ws.onopen = () => settle();
        let errorFallback: ReturnType<typeof setTimeout> | null = null;
        ws.onerror = () => {
          if (!settled) {
            errorFallback = setTimeout(() => {
              if (!settled) settle(new Error(`连接失败: ${url}`));
            }, 100);
          }
        };
        ws.onclose = (ev) => {
          if (errorFallback) clearTimeout(errorFallback);
          if (settled) return;
          const reason = ev.reason?.trim() || closeCodeMessage(ev.code);
          settle(new Error(`连接失败: ${reason} (code ${ev.code})`));
        };
      });
      ws.binaryType = 'arraybuffer';
      ws.onmessage = (ev) => {
        try {
          const data = new Uint8Array(ev.data as ArrayBuffer);
          if (data.length < 5) return;
          const methodId = data[0];
          const invokeId = data[1] | (data[2] << 8);
          const payloadLen = data[3] | (data[4] << 8);
          if (data.length < 5 + payloadLen) return;
          const payload = data.subarray(5, 5 + payloadLen);

          if (invokeId === 0) {
            const cb = streamSubs.get(methodId);
            if (cb) cb(payload);
          } else {
            const h = pending.get(invokeId);
            if (h) {
              pending.delete(invokeId);
              h.resolve(payload);
            }
          }
        } catch (_) {}
      };
    },
    disconnect(): void {
      if (ws) {
        ws.close();
        ws = null;
      }
      pending.forEach((h) => h.reject(new Error('Disconnected')));
      pending.clear();
    },
  };
}
