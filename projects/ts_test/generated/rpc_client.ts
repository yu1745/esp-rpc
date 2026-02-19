/* Auto-generated - do not edit */

import type { CreateUserRequest, User, UserResponse } from './rpc_types';
import type { EsprpcTransport } from './transport';

export class UserServiceClient {
  readonly #transport: EsprpcTransport;
  constructor(transport: EsprpcTransport) {
    this.#transport = transport;
  }

  async GetUser(_id: number): Promise<UserResponse> {
    return this.#transport.call<UserResponse>(0, arguments);
  }

  async CreateUser(_request: CreateUserRequest): Promise<UserResponse> {
    return this.#transport.call<UserResponse>(1, arguments);
  }

  CreateUserV2(_request: CreateUserRequest): void {
    this.#transport.sendStreamRequest(2, arguments);
  }

  async UpdateUser(_id: number, _request: CreateUserRequest): Promise<UserResponse> {
    return this.#transport.call<UserResponse>(3, arguments);
  }

  async DeleteUser(_id: number): Promise<boolean> {
    return this.#transport.call<boolean>(4, arguments);
  }

  async ListUsers(_page: number | undefined): Promise<User[]> {
    return this.#transport.call<User[]>(5, arguments, { timeout: 5000 });
  }

  WatchUsers(): { subscribe(cb: (v: User) => void): () => void } {
    return { subscribe: (cb) => {
      this.#transport.subscribe<User>(6, cb);
      this.#transport.sendStreamRequest(6, []);
      return () => this.#transport.unsubscribe(6);
    } };
  }

  Ping(): void {
    this.#transport.sendStreamRequest(7, arguments);
  }

}
