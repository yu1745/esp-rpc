export interface EsprpcTransport {
  call(methodId: number, payload: Uint8Array, options?: { timeout?: number }): Promise<Uint8Array>;
  subscribe(methodId: number, cb: (data: Uint8Array) => void): void;
  unsubscribe(methodId: number): void;
  connect(): Promise<void>;
  disconnect(): void;
}
