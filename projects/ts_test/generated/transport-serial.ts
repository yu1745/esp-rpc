/**
 * 串口传输实现（Web Serial API，二进制协议）
 *
 * 与 ESP32 transport_serial.c 使用相同帧格式: [1B method_id][2B invoke_id LE][2B payload_len LE][payload]
 * 若指定 prefix/suffix，收发时自动插入与剥离，与 ESP 端 Kconfig 前后缀一致即可复用串口。
 * 需在 HTTPS 或 localhost 下使用；用户需在浏览器弹窗中选择串口设备。
 */

import type { EsprpcTransport } from './transport';
import { encodeRequest, decodeResponse } from './rpc_binary_codec';

function toMarkerBytes(v: string | number[] | Uint8Array | undefined): Uint8Array {
  if (v === undefined || v === null) return new Uint8Array(0);
  if (typeof v === 'string') return new Uint8Array([...v].map(c => c.charCodeAt(0) & 0xff));
  if (v instanceof Uint8Array) return v;
  return new Uint8Array(v as number[]);
}

export function createSerialTransport(options?: { baudRate?: number; prefix?: string | number[] | Uint8Array; suffix?: string | number[] | Uint8Array }): EsprpcTransport {
  const baudRate = options?.baudRate ?? 115200;
  const prefixBytes = toMarkerBytes(options?.prefix);
  const suffixBytes = toMarkerBytes(options?.suffix);
  const prefixLen = prefixBytes.length;
  const suffixLen = suffixBytes.length;
  let port: SerialPort | null = null;
  let reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  let invokeIdCounter = 1;
  const pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void; timeoutId: ReturnType<typeof setTimeout> }>();
  const streamSubs = new Map<number, (data: unknown) => void>();

  async function sendFrame(frame: Uint8Array): Promise<void> {
    if (!port?.writable) return;
    const total = prefixLen + frame.length + suffixLen;
    const packet = new Uint8Array(total);
    let off = 0;
    if (prefixLen) { packet.set(prefixBytes, off); off += prefixLen; }
    packet.set(frame, off); off += frame.length;
    if (suffixLen) packet.set(suffixBytes, off);
    const writer = port.writable.getWriter();
    try {
      await writer.write(packet);
    } finally {
      writer.releaseLock();
    }
  }

  function findPrefix(buf: number[], prefix: Uint8Array): number {
    const plen = prefix.length;
    if (buf.length < plen) return buf.length;
    for (let i = 0; i <= buf.length - plen; i++) {
      let match = true;
      for (let j = 0; j < plen; j++) if (buf[i + j] !== prefix[j]) { match = false; break; }
      if (match) return i;
    }
    return buf.length;
  }

  function runReadLoop(): void {
    if (!port?.readable || !reader) return;
    void (async () => {
      const buf: number[] = [];
      let need = 5;
      while (true) {
        const { value, done } = await reader!.read();
        if (done) break;
        for (let i = 0; i < value!.length; i++) buf.push(value![i]);
        if (prefixLen > 0) {
          const idx = findPrefix(buf, prefixBytes);
          if (idx >= buf.length) continue;
          if (idx > 0) buf.splice(0, idx);
          if (buf.length < prefixLen) continue;
          buf.splice(0, prefixLen);
        }
        while (buf.length >= need) {
          if (need === 5) {
            if (buf.length < 5) break;
            const payloadLen = buf[3]! | (buf[4]! << 8);
            need = 5 + payloadLen;
          }
          if (buf.length < need) break;
          const frame = new Uint8Array(buf.splice(0, need));
          need = 5;
          if (frame.length < 5) continue;
          if (suffixLen > 0 && buf.length >= suffixLen) buf.splice(0, suffixLen);
          const methodId = frame[0]!;
          const invokeId = frame[1]! | (frame[2]! << 8);
          const payloadLen = frame[3]! | (frame[4]! << 8);
          const payload = frame.subarray(5, 5 + payloadLen);
          try {
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
        }
      }
    })();
  }

  return {
    async call<T = unknown>(methodId: number, args: IArguments, options?: { timeout?: number }): Promise<T> {
      return new Promise((resolve, reject) => {
        if (!port) {
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
        sendFrame(frame);
      });
    },
    sendStreamRequest(methodId: number, args?: IArguments | unknown[]): void {
      if (!port) return;
      const payload = encodeRequest(methodId, args ?? { length: 0 } as IArguments);
      const frame = new Uint8Array(5 + payload.length);
      frame[0] = methodId;
      frame[1] = 0;
      frame[2] = 0;
      frame[3] = payload.length & 0xff;
      frame[4] = (payload.length >> 8) & 0xff;
      frame.set(payload, 5);
      sendFrame(frame);
    },
    subscribe<T = unknown>(methodId: number, cb: (data: T) => void): void {
      streamSubs.set(methodId, cb as (data: unknown) => void);
    },
    unsubscribe(methodId: number): void {
      streamSubs.delete(methodId);
    },
    async connect(): Promise<void> {
      if (typeof navigator === 'undefined' || !(navigator as unknown as { serial?: unknown }).serial) {
        throw new Error('Web Serial API 不可用，请使用 Chrome/Edge 或 HTTPS');
      }
      const nav = navigator as unknown as { serial: { requestPort: () => Promise<SerialPort> } };
      port = await nav.serial.requestPort();
      await port.open({ baudRate });
      reader = port.readable!.getReader();
      runReadLoop();
    },
    disconnect(): void {
      if (reader) {
        reader.cancel();
        reader = null;
      }
      if (port) {
        try { port.close(); } catch (_) {}
        port = null;
      }
      pending.forEach((h) => { clearTimeout(h.timeoutId); h.reject(new Error('Disconnected')); });
      pending.clear();
    },
  };
}

/** Node.js serialport 包 SerialPort 实例的接口（直接传入已打开的 port，用于 Electron/Node）。data 为 Buffer 时与 Uint8Array 兼容。 */
export interface NodeSerialPortLike {
  readonly isOpen: boolean;
  write(data: Uint8Array | number[], callback?: (err?: Error | null) => void): boolean;
  on(event: 'data', listener: (data: Uint8Array) => void): this;
  off?(event: 'data', listener: (data: Uint8Array) => void): this;
  removeListener?(event: 'data', listener: (data: Uint8Array) => void): this;
}

/**
 * 基于 Node.js serialport 包 SerialPort 实例创建传输层。
 * 直接传入已打开的 SerialPort，不负责 open/close，仅负责收发与帧解析。帧格式同 createSerialTransport。
 */
export function createSerialTransportFromPort(
  port: NodeSerialPortLike,
  options?: { prefix?: string | number[] | Uint8Array; suffix?: string | number[] | Uint8Array }
): EsprpcTransport {
  const prefixBytes = toMarkerBytes(options?.prefix);
  const suffixBytes = toMarkerBytes(options?.suffix);
  const prefixLen = prefixBytes.length;
  const suffixLen = suffixBytes.length;
  let invokeIdCounter = 1;
  const pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void; timeoutId: ReturnType<typeof setTimeout> }>();
  const streamSubs = new Map<number, (data: unknown) => void>();

  function findPrefix(buf: number[], prefix: Uint8Array): number {
    const plen = prefix.length;
    if (buf.length < plen) return buf.length;
    for (let i = 0; i <= buf.length - plen; i++) {
      let match = true;
      for (let j = 0; j < plen; j++) if (buf[i + j] !== prefix[j]) { match = false; break; }
      if (match) return i;
    }
    return buf.length;
  }

  const buf: number[] = [];
  let need = 5;

  function onData(chunk: Uint8Array): void {
    for (let i = 0; i < chunk.length; i++) buf.push(chunk[i]!);
    if (prefixLen > 0) {
      const idx = findPrefix(buf, prefixBytes);
      if (idx >= buf.length) return;
      if (idx > 0) buf.splice(0, idx);
      if (buf.length < prefixLen) return;
      buf.splice(0, prefixLen);
    }
    while (buf.length >= need) {
      if (need === 5) {
        if (buf.length < 5) break;
        const payloadLen = buf[3]! | (buf[4]! << 8);
        need = 5 + payloadLen;
      }
      if (buf.length < need) break;
      const frame = new Uint8Array(buf.splice(0, need));
      need = 5;
      if (frame.length < 5) continue;
      if (suffixLen > 0 && buf.length >= suffixLen) buf.splice(0, suffixLen);
      const methodId = frame[0]!;
      const invokeId = frame[1]! | (frame[2]! << 8);
      const payloadLen = frame[3]! | (frame[4]! << 8);
      const payload = frame.subarray(5, 5 + payloadLen);
      try {
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
    }
  }

  function sendFrame(frame: Uint8Array): void {
    if (!port.isOpen) return;
    const total = prefixLen + frame.length + suffixLen;
    const packet = new Uint8Array(total);
    let off = 0;
    if (prefixLen) { packet.set(prefixBytes, off); off += prefixLen; }
    packet.set(frame, off); off += frame.length;
    if (suffixLen) packet.set(suffixBytes, off);
    port.write(packet);
  }

  function removeDataListener(): void {
    if (port.off) port.off('data', onData);
    else if (port.removeListener) port.removeListener('data', onData);
  }

  return {
    async call<T = unknown>(methodId: number, args: IArguments, options?: { timeout?: number }): Promise<T> {
      return new Promise((resolve, reject) => {
        if (!port.isOpen) {
          reject(new Error('Port is not open'));
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
        sendFrame(frame);
      });
    },
    sendStreamRequest(methodId: number, args?: IArguments | unknown[]): void {
      if (!port.isOpen) return;
      const payload = encodeRequest(methodId, args ?? { length: 0 } as IArguments);
      const frame = new Uint8Array(5 + payload.length);
      frame[0] = methodId;
      frame[1] = 0;
      frame[2] = 0;
      frame[3] = payload.length & 0xff;
      frame[4] = (payload.length >> 8) & 0xff;
      frame.set(payload, 5);
      sendFrame(frame);
    },
    subscribe<T = unknown>(methodId: number, cb: (data: T) => void): void {
      streamSubs.set(methodId, cb as (data: unknown) => void);
    },
    unsubscribe(methodId: number): void {
      streamSubs.delete(methodId);
    },
    async connect(): Promise<void> {
      if (!port.isOpen) {
        throw new Error('Port is not open. Open the serialport before calling connect().');
      }
      port.on('data', onData);
    },
    disconnect(): void {
      removeDataListener();
      pending.forEach((h) => { clearTimeout(h.timeoutId); h.reject(new Error('Disconnected')); });
      pending.clear();
      buf.length = 0;
      need = 5;
    },
  };
}
