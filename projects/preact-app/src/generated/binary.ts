export class BufferWriter {
  private chunks: Uint8Array[] = [];
  private totalSize = 0;

  writeU8(v: number) {
    this.chunks.push(new Uint8Array([v & 0xff]));
    this.totalSize += 1;
  }

  writeU16(v: number) {
    this.chunks.push(new Uint8Array([v & 0xff, (v >> 8) & 0xff]));
    this.totalSize += 2;
  }

  writeU32(v: number) {
    this.chunks.push(new Uint8Array([v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff]));
    this.totalSize += 4;
  }

  writeI32(v: number) { this.writeU32(v >>> 0); }

  writeBool(v: boolean) { this.writeU8(v ? 1 : 0); }

  writeStr(s: string) {
    const enc = new TextEncoder().encode(s ?? '');
    this.writeU16(enc.length);
    this.chunks.push(enc);
    this.totalSize += enc.length;
  }

  toBytes(): Uint8Array {
    const result = new Uint8Array(this.totalSize);
    let offset = 0;
    for (const chunk of this.chunks) {
      result.set(chunk, offset);
      offset += chunk.length;
    }
    return result;
  }
}

export class BufferReader {
  private offset = 0;
  constructor(private data: Uint8Array) {}

  readU8(): number { return this.data[this.offset++]; }

  readU16(): number {
    const v = this.data[this.offset] | (this.data[this.offset + 1] << 8);
    this.offset += 2;
    return v;
  }

  readU32(): number {
    const v = this.data[this.offset] | (this.data[this.offset + 1] << 8) |
      (this.data[this.offset + 2] << 16) | (this.data[this.offset + 3] << 24);
    this.offset += 4;
    return v >>> 0;
  }

  readI32(): number { return this.readU32() | 0; }

  readBool(): boolean { return this.readU8() !== 0; }

  readStr(): string {
    const len = this.readU16();
    const slice = this.data.subarray(this.offset, this.offset + len);
    this.offset += len;
    return new TextDecoder().decode(slice);
  }

  skip(n: number) { this.offset += n; }

  get remaining() { return this.data.length - this.offset; }
}
