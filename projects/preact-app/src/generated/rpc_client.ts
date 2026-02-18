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

  async UpdateUser(_id: number, _request: CreateUserRequest): Promise<UserResponse> {
    return this.#transport.call<UserResponse>(2, arguments);
  }

  async DeleteUser(_id: number): Promise<boolean> {
    return this.#transport.call<boolean>(3, arguments);
  }

  async ListUsers(_page: number | undefined): Promise<User[]> {
    return this.#transport.call<User[]>(4, arguments, { timeout: 5000 });
  }

  WatchUsers(): { subscribe(cb: (v: User) => void): () => void } {
    return { subscribe: (cb) => {
      this.#transport.subscribe<User>(5, cb);
      this.#transport.sendStreamRequest(5, []);
      return () => this.#transport.unsubscribe(5);
    } };
  }

}
