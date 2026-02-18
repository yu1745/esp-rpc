/* Auto-generated - do not edit */

export const UserStatus = {
  ACTIVE: 1,
  INACTIVE: 2,
  DELETED: 3,
} as const;
export type UserStatus = typeof UserStatus[keyof typeof UserStatus];

export interface User {
  id: number;
  name: string;
  email?: string | undefined;
  status: UserStatus;
  tags: string[];
}

export interface CreateUserRequest {
  name: string;
  email: string;
  password?: string | undefined;
}

export interface UserResponse {
  id: number;
  name: string;
  email: string;
  status: UserStatus;
}
