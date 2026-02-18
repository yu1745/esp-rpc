/* Auto-generated - do not edit */

import type { User, CreateUserRequest, UserResponse } from './rpc_types';

export function encodeRequest(methodId: number, args: IArguments | unknown[]): Uint8Array {
  const argsOrArray = args.length !== undefined ? Array.from(args as IArguments) : (args as unknown[]);

  if (methodId === 0) return new Uint8Array(0);
    if (methodId === 1) {
      const args = Array.from(argsOrArray);
      let off = 0;
      let buf = new ArrayBuffer(256);
      let dv = new DataView(buf);
      const ensure = (n: number) => {
        if (off + n > buf.byteLength) {
          const newBuf = new ArrayBuffer(buf.byteLength * 2);
          new Uint8Array(newBuf).set(new Uint8Array(buf));
          buf = newBuf; dv = new DataView(buf);
        }
      };
      ensure(4); dv.setInt32(off, args[0] | 0, true); off += 4;
      return new Uint8Array(buf, 0, off);
    }
    if (methodId === 2) {
      const args = Array.from(argsOrArray);
      let off = 0;
      let buf = new ArrayBuffer(256);
      let dv = new DataView(buf);
      const ensure = (n: number) => {
        if (off + n > buf.byteLength) {
          const newBuf = new ArrayBuffer(buf.byteLength * 2);
          new Uint8Array(newBuf).set(new Uint8Array(buf));
          buf = newBuf; dv = new DataView(buf);
        }
      };
      const _sname = args[0].name ?? ""; const _sbname = new TextEncoder().encode(_sname);
      ensure(2 + _sbname.length); dv.setUint16(off, _sbname.length, true); off += 2;
      new Uint8Array(buf).set(_sbname, off); off += _sbname.length;
      const _semail = args[0].email ?? ""; const _sbemail = new TextEncoder().encode(_semail);
      ensure(2 + _sbemail.length); dv.setUint16(off, _sbemail.length, true); off += 2;
      new Uint8Array(buf).set(_sbemail, off); off += _sbemail.length;
      ensure(1);
      if (args[0].password !== undefined && args[0].password !== null) {
        dv.setUint8(off, 1); off += 1;
        const _spassword = args[0].password; const _sbpassword = new TextEncoder().encode(_spassword);
        ensure(2 + _sbpassword.length); dv.setUint16(off, _sbpassword.length, true); off += 2;
        new Uint8Array(buf).set(_sbpassword, off); off += _sbpassword.length;
      } else { dv.setUint8(off, 0); off += 1; }
      return new Uint8Array(buf, 0, off);
    }
    if (methodId === 3) {
      const args = Array.from(argsOrArray);
      let off = 0;
      let buf = new ArrayBuffer(256);
      let dv = new DataView(buf);
      const ensure = (n: number) => {
        if (off + n > buf.byteLength) {
          const newBuf = new ArrayBuffer(buf.byteLength * 2);
          new Uint8Array(newBuf).set(new Uint8Array(buf));
          buf = newBuf; dv = new DataView(buf);
        }
      };
      ensure(4); dv.setInt32(off, args[0] | 0, true); off += 4;
      const _sname = args[1].name ?? ""; const _sbname = new TextEncoder().encode(_sname);
      ensure(2 + _sbname.length); dv.setUint16(off, _sbname.length, true); off += 2;
      new Uint8Array(buf).set(_sbname, off); off += _sbname.length;
      const _semail = args[1].email ?? ""; const _sbemail = new TextEncoder().encode(_semail);
      ensure(2 + _sbemail.length); dv.setUint16(off, _sbemail.length, true); off += 2;
      new Uint8Array(buf).set(_sbemail, off); off += _sbemail.length;
      ensure(1);
      if (args[1].password !== undefined && args[1].password !== null) {
        dv.setUint8(off, 1); off += 1;
        const _spassword = args[1].password; const _sbpassword = new TextEncoder().encode(_spassword);
        ensure(2 + _sbpassword.length); dv.setUint16(off, _sbpassword.length, true); off += 2;
        new Uint8Array(buf).set(_sbpassword, off); off += _sbpassword.length;
      } else { dv.setUint8(off, 0); off += 1; }
      return new Uint8Array(buf, 0, off);
    }
    if (methodId === 4) {
      const args = Array.from(argsOrArray);
      let off = 0;
      let buf = new ArrayBuffer(256);
      let dv = new DataView(buf);
      const ensure = (n: number) => {
        if (off + n > buf.byteLength) {
          const newBuf = new ArrayBuffer(buf.byteLength * 2);
          new Uint8Array(newBuf).set(new Uint8Array(buf));
          buf = newBuf; dv = new DataView(buf);
        }
      };
      ensure(4); dv.setInt32(off, args[0] | 0, true); off += 4;
      return new Uint8Array(buf, 0, off);
    }
    if (methodId === 5) {
      const args = Array.from(argsOrArray);
      let off = 0;
      let buf = new ArrayBuffer(256);
      let dv = new DataView(buf);
      const ensure = (n: number) => {
        if (off + n > buf.byteLength) {
          const newBuf = new ArrayBuffer(buf.byteLength * 2);
          new Uint8Array(newBuf).set(new Uint8Array(buf));
          buf = newBuf; dv = new DataView(buf);
        }
      };
      ensure(1);
      if (args[0] !== undefined && args[0] !== null) {
        dv.setUint8(off, 1); off += 1; ensure(4); dv.setInt32(off, args[0] | 0, true); off += 4;
      } else { dv.setUint8(off, 0); off += 1; }
      return new Uint8Array(buf, 0, off);
    }
  throw new Error(`Unknown methodId: ${methodId}`);
}

export function decodeResponse<T = unknown>(methodId: number, payload: Uint8Array): T {

    if (methodId === 0) {
      const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      let off = 0;
      const result: any = {};
      result.id = dv.getInt32(off, true); off += 4;
      const _lname = dv.getUint16(off, true); off += 2;
      result.name = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lname)); off += _lname;
      const _pemail = dv.getUint8(off); off += 1;
      if (_pemail) { const _lemail = dv.getUint16(off, true); off += 2;
        result.email = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lemail)); off += _lemail; }
      result.status = dv.getInt32(off, true); off += 4;
      const _ltags = dv.getUint16(off, true); off += 2;
      result.tags = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _ltags)); off += _ltags;
      return result;
    }
    if (methodId === 1) {
      const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      let off = 0;
      const result: any = {};
      result.id = dv.getInt32(off, true); off += 4;
      const _lname = dv.getUint16(off, true); off += 2;
      result.name = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lname)); off += _lname;
      const _lemail = dv.getUint16(off, true); off += 2;
      result.email = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lemail)); off += _lemail;
      result.status = dv.getInt32(off, true); off += 4;
      return result;
    }
    if (methodId === 2) {
      const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      let off = 0;
      const result: any = {};
      result.id = dv.getInt32(off, true); off += 4;
      const _lname = dv.getUint16(off, true); off += 2;
      result.name = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lname)); off += _lname;
      const _lemail = dv.getUint16(off, true); off += 2;
      result.email = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lemail)); off += _lemail;
      result.status = dv.getInt32(off, true); off += 4;
      return result;
    }
    if (methodId === 3) {
      const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      let off = 0;
      const result: any = {};
      result.id = dv.getInt32(off, true); off += 4;
      const _lname = dv.getUint16(off, true); off += 2;
      result.name = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lname)); off += _lname;
      const _lemail = dv.getUint16(off, true); off += 2;
      result.email = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lemail)); off += _lemail;
      result.status = dv.getInt32(off, true); off += 4;
      return result;
    }
    if (methodId === 4) {
      const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      let off = 0;
      return dv.getUint8(off) !== 0;
    }
    if (methodId === 5) {
      const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      let off = 0;
      const count = dv.getUint32(off, true); off += 4;
      const items: any[] = [];
      for (let i = 0; i < count; i++) {
        const item: any = {};
        item.id = dv.getInt32(off, true); off += 4;
        const _lname = dv.getUint16(off, true); off += 2;
        item.name = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lname)); off += _lname;
        const _pemail = dv.getUint8(off); off += 1;
        if (_pemail) { const _lemail = dv.getUint16(off, true); off += 2;
          item.email = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _lemail)); off += _lemail; }
        item.status = dv.getInt32(off, true); off += 4;
        const _ltags = dv.getUint16(off, true); off += 2;
        item.tags = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _ltags)); off += _ltags;
        items.push(item);
      }
      return { items, len: items.length };
    }
  throw new Error(`Unknown methodId: ${methodId}`);
}