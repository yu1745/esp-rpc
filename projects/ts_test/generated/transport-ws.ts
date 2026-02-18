/**
 * WebSocket 传输实现（二进制协议）
 */

import type { EsprpcTransport } from './transport';
import { encodeRequest, decodeResponse } from './rpc_binary_codec';

function closeCodeMessage(code: number): string {
  const map: Record<number, string> = {
    1000: '正常关闭',
    1001: '端点离开',
    1002: '协议错误',
    1006: '异常关闭',
    1011: '服务器内部错误',
  };
  return map[code] ?? `未知错误`;
}

export function createWebSocketTransport(url: string): EsprpcTransport {
  let ws: WebSocket | null = null;
  let invokeIdCounter = 1;
  const pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void; timeoutId: ReturnType<typeof setTimeout> }>();
  const streamSubs = new Map<number, (data: unknown) => void>();

  return {
    async call<T = unknown>(methodId: number, args: IArguments, options?: { timeout?: number }): Promise<T> {
      return new Promise((resolve, reject) => {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          reject(new Error('Not connected'));
          return;
        }
        const invokeId = invokeIdCounter++;
        if (invokeIdCounter > 0xfffe) invokeIdCounter = 1;
        const timeoutMs = options?.timeout ?? 2000;
        const timeoutId = setTimeout(() => {
          const h = pending.get(invokeId);
          if (h) {
            pending.delete(invokeId);
            reject(new Error(`RPC 超时 (${timeoutMs}ms)`));
          }
        }, timeoutMs);
        pending.set(invokeId, {
          resolve: (v) => { clearTimeout(timeoutId); (resolve as (v: unknown) => void)(v); },
          reject: (e) => { clearTimeout(timeoutId); reject(e); },
          timeoutId,
        });
        const payload = encodeRequest(methodId, args);
        const frame = new Uint8Array(5 + payload.length);
        frame[0] = methodId;
        frame[1] = invokeId & 0xff;
        frame[2] = (invokeId >> 8) & 0xff;
        frame[3] = payload.length & 0xff;
        frame[4] = (payload.length >> 8) & 0xff;
        frame.set(payload, 5);
        ws.send(frame);
      });
    },
    sendStreamRequest(methodId: number, args?: IArguments | unknown[]): void {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      const payload = encodeRequest(methodId, args ?? { length: 0 } as IArguments);
      const frame = new Uint8Array(5 + payload.length);
      frame[0] = methodId;
      frame[1] = 0;
      frame[2] = 0;
      frame[3] = payload.length & 0xff;
      frame[4] = (payload.length >> 8) & 0xff;
      frame.set(payload, 5);
      ws.send(frame);
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
          if (!settled) {
            errorFallback = setTimeout(() => {
              if (!settled) settle(new Error(`连接失败: ${url}`));
            }, 100);
          }
        };
        ws.onclose = (ev) => {
          if (errorFallback) clearTimeout(errorFallback);
          if (settled) return;
          settle(new Error(`连接失败: ${ev.reason || closeCodeMessage(ev.code)} (code ${ev.code})`));
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
          const result = decodeResponse(methodId, payload);
          if (invokeId !== 0) {
            const h = pending.get(invokeId);
            if (h) {
              pending.delete(invokeId);
              h.resolve(result);
            }
          } else {
            const cb = streamSubs.get(methodId);
            if (cb) cb(result);
          }
        } catch (_) {}
      };
    },
    disconnect(): void {
      if (ws) { ws.close(); ws = null; }
      pending.forEach((h) => { clearTimeout(h.timeoutId); h.reject(new Error('Disconnected')); });
      pending.clear();
    },
    };
}
