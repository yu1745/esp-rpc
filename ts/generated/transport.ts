/**
 * RPC 传输层抽象接口
 */

export interface EsprpcTransport {
  call<T = unknown>(methodId: number, args: IArguments, options?: { timeout?: number }): Promise<T>;
  subscribe<T = unknown>(methodId: number, cb: (data: T) => void): void;
  unsubscribe(methodId: number): void;
  connect(): Promise<void>;
  disconnect(): void;
}
