/**
 * WebSocket 传输实现
 */

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

export function createWebSocketTransport(url: string): EsprpcTransport {
  let ws: WebSocket | null = null;
  const pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void }>();
  const streamSubs = new Map<number, (data: unknown) => void>();

  return {
    async call<T = unknown>(methodId: number, args: IArguments, options?: { timeout?: number }): Promise<T> {
      return new Promise((resolve, reject) => {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          reject(new Error('Not connected'));
          return;
        }
        const timeoutMs = options?.timeout ?? 10000;
        const t = setTimeout(() => {
          if (pending.delete(methodId)) {
            reject(new Error(`RPC 超时 (${timeoutMs}ms)`));
          }
        }, timeoutMs);
        pending.set(methodId, {
          resolve: (v) => {
            clearTimeout(t);
            (resolve as (v: unknown) => void)(v);
          },
          reject: (e) => {
            clearTimeout(t);
            reject(e);
          },
        });
        /* 二进制帧: [1B methodId][2B payload_len LE][JSON payload] */
        const payload = JSON.stringify({ args: Array.from(args) });
        const enc = new TextEncoder().encode(payload);
        const frame = new Uint8Array(3 + enc.length);
        frame[0] = methodId;
        frame[1] = enc.length & 0xff;
        frame[2] = (enc.length >> 8) & 0xff;
        frame.set(enc, 3);
        ws.send(frame.buffer);
      });
    },
    subscribe<T = unknown>(methodId: number, cb: (data: T) => void): void {
      streamSubs.set(methodId, cb as (data: unknown) => void);
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
          // onclose 通常紧随 onerror 触发且含 code/reason，延迟兜底以便优先使用 onclose 的详细信息
          if (!settled) {
            errorFallback = setTimeout(() => {
              if (!settled) settle(new Error(`连接失败: ${url}，请检查地址、网络或设备是否在线`));
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
          if (data.length < 3) return;
          const methodId = data[0];
          const payloadLen = data[1] | (data[2] << 8);
          if (data.length < 3 + payloadLen) return;
          const payload = new TextDecoder().decode(data.subarray(3, 3 + payloadLen));
          const msg = JSON.parse(payload) as { result?: unknown; data?: unknown };
          const h = pending.get(methodId);
          if (h) {
            pending.delete(methodId);
            h.resolve(msg.result);
          } else {
            const cb = streamSubs.get(methodId);
            if (cb) cb(msg.data);
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
