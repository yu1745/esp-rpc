/**
 * RPC 传输层抽象接口
 */

export interface EsprpcTransport {
  call<T = unknown>(methodId: number, args: IArguments, options?: { timeout?: number }): Promise<T>;
  /** 发送 stream 请求（不等待响应，数据通过 subscribe 回调接收） */
  sendStreamRequest(methodId: number, args?: IArguments | unknown[]): void;
  subscribe<T = unknown>(methodId: number, cb: (data: T) => void): void;
  unsubscribe(methodId: number): void;
  connect(): Promise<void>;
  disconnect(): void;
}
