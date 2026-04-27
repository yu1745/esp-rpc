import { BufferWriter, BufferReader } from './binary';
import type { EsprpcTransport } from './transport';
import type { User, UserResponse, CreateUserRequest, UserStatus } from './rpc_types';

function userFromReader(r: BufferReader): User {
  return {
    id: r.i32(),
    name: r.str(),
    email: r.bool() ? r.str() : undefined,
    status: r.u32() as UserStatus,
    tags: [],
    metadata: {},
  };
}

function userResponseFromReader(r: BufferReader): UserResponse {
  return {
    id: r.i32(),
    name: r.str(),
    email: r.str(),
    status: r.u32() as UserStatus,
  };
}

export class UserServiceClient {
  readonly #transport: EsprpcTransport;
  constructor(transport: EsprpcTransport) {
    this.#transport = transport;
  }

  WatchUsers(): { subscribe(cb: (v: User) => void): () => void } {
    return {
      subscribe: (cb) => {
        this.#transport.subscribe(0, (data) => {
          const r = new BufferReader(data);
          cb(userFromReader(r));
        });
        return () => this.#transport.unsubscribe(0);
      },
    };
  }

  async GetUser(id: number): Promise<UserResponse> {
    const w = new BufferWriter();
    w.i32(id);
    const resp = await this.#transport.call(1, w.toBytes());
    const r = new BufferReader(resp);
    return userResponseFromReader(r);
  }

  async CreateUser(request: CreateUserRequest): Promise<UserResponse> {
    const w = new BufferWriter();
    w.str(request.name);
    w.str(request.email);
    w.bool(request.password != null);
    if (request.password != null) w.str(request.password!);
    const resp = await this.#transport.call(2, w.toBytes());
    const r = new BufferReader(resp);
    return userResponseFromReader(r);
  }

  async UpdateUser(id: number, request: CreateUserRequest): Promise<UserResponse> {
    const w = new BufferWriter();
    w.i32(id);
    w.str(request.name);
    w.str(request.email);
    w.bool(request.password != null);
    if (request.password != null) w.str(request.password!);
    const resp = await this.#transport.call(3, w.toBytes());
    const r = new BufferReader(resp);
    return userResponseFromReader(r);
  }

  async DeleteUser(id: number): Promise<number> {
    const w = new BufferWriter();
    w.i32(id);
    const resp = await this.#transport.call(4, w.toBytes());
    const r = new BufferReader(resp);
    return r.i32();
  }

  async ListUsers(page: number | undefined): Promise<User[]> {
    const w = new BufferWriter();
    w.bool(page != null);
    if (page != null) w.i32(page);
    const resp = await this.#transport.call(5, w.toBytes(), { timeout: 5000 });
    const r = new BufferReader(resp);
    const count = r.u32();
    const users: User[] = [];
    for (let i = 0; i < count; i++) {
      users.push(userFromReader(r));
    }
    return users;
  }

  CreateUserV2(request: CreateUserRequest): void {
    const w = new BufferWriter();
    w.str(request.name);
    w.str(request.email);
    w.bool(request.password != null);
    if (request.password != null) w.str(request.password!);
    this.#transport.call(6, w.toBytes()).catch(() => {});
  }
}
