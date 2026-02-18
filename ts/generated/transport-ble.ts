/**
 * BLE 传输实现（Web Bluetooth API，二进制协议）
 *
 * 服务 UUID: 0000E530-1212-EFDE-1523-785FEABCD123
 * TX 特征 (写): 0000E531-...  RX 特征 (通知): 0000E532-...
 */

import type { EsprpcTransport } from './transport';
import { encodeRequest, decodeResponse } from './rpc_binary_codec';

const ESPRPC_SERVICE_UUID = '0000e530-1212-efde-1523-785feabcd123';
const ESPRPC_CHR_TX_UUID = '0000e531-1212-efde-1523-785feabcd123';
const ESPRPC_CHR_RX_UUID = '0000e532-1212-efde-1523-785feabcd123';

export function createBleTransport(): EsprpcTransport {
  let device: BluetoothDevice | null = null;
  let txChar: BluetoothRemoteGATTCharacteristic | null = null;
  let rxChar: BluetoothRemoteGATTCharacteristic | null = null;
  let invokeIdCounter = 1;
  const pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void; timeoutId: ReturnType<typeof setTimeout> }>();
  const streamSubs = new Map<number, (data: unknown) => void>();

  function sendFrame(frame: Uint8Array): void {
    if (!txChar) return;
    txChar.writeValueWithoutResponse(frame);
  }

  return {
    async call<T = unknown>(methodId: number, args: IArguments, options?: { timeout?: number }): Promise<T> {
      return new Promise((resolve, reject) => {
        if (!txChar) {
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
      if (!txChar) return;
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
      if (typeof navigator === 'undefined' || !navigator.bluetooth) {
        throw new Error('Web Bluetooth API 不可用，请使用 HTTPS 或 Chrome');
      }
      device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [ESPRPC_SERVICE_UUID] }],
        optionalServices: [ESPRPC_SERVICE_UUID],
      });
      const server = await device.gatt!.connect();
      const service = await server.getPrimaryService(ESPRPC_SERVICE_UUID);
      txChar = await service.getCharacteristic(ESPRPC_CHR_TX_UUID);
      rxChar = await service.getCharacteristic(ESPRPC_CHR_RX_UUID);
      await rxChar.startNotifications();
      rxChar.addEventListener('characteristicvaluechanged', (ev: Event) => {
        const target = ev.target as BluetoothRemoteGATTCharacteristic;
        const value = target?.value;
        if (!value || value.byteLength < 5) return;
        try {
          const data = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
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
      });
    },
    disconnect(): void {
      if (device?.gatt?.connected) {
        device.gatt.disconnect();
      }
      device = null;
      txChar = null;
      rxChar = null;
      pending.forEach((h) => { clearTimeout(h.timeoutId); h.reject(new Error('Disconnected')); });
      pending.clear();
    },
  };
}
