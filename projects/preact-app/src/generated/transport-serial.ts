import type { EsprpcTransport } from './transport';

export function createSerialTransport(_opts: { baudRate?: number; prefix?: string; suffix?: string }): EsprpcTransport {
  throw new Error('Serial transport not yet implemented');
}
