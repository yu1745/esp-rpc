"""
TypeScript 二进制编解码生成器
与 C 端 binary protocol 保持一致
"""

try:
    from .parser import RpcSchema, ServiceDef, MethodDef, StructDef, StructField
except ImportError:
    from parser import RpcSchema, ServiceDef, MethodDef, StructDef, StructField


def _unwrap_type(type_str: str) -> str:
    t = type_str.strip()
    if t.startswith('REQUIRED(') and t.endswith(')'):
        return t[9:-1].strip()
    if t.startswith('OPTIONAL(') and t.endswith(')'):
        return t[9:-1].strip()
    if t.startswith('LIST(') and t.endswith(')'):
        return t[5:-1].strip()
    return t


def _c_primitive(type_str: str) -> bool:
    t = type_str.strip()
    return t in ('int', 'int32', 'int64', 'uint32', 'uint64', 'bool', 'float', 'double')


def _get_struct(schema: RpcSchema, name: str) -> StructDef | None:
    for s in schema.structs:
        if s.name == name:
            return s
    return None


def _is_string_type(type_str: str) -> bool:
    return _unwrap_type(type_str) == 'string'


def _is_struct_param(type_str: str, schema: RpcSchema) -> bool:
    t = type_str.strip()
    if t.startswith('LIST(') or t.startswith('STREAM(') or t.startswith('OPTIONAL('):
        inner = _unwrap_type(t)
        if _c_primitive(inner) or inner == 'string':
            return False
        return _get_struct(schema, inner) is not None
    return _get_struct(schema, t) is not None


def _is_enum_type(type_str: str, schema: RpcSchema) -> bool:
    base = _unwrap_type(type_str)
    return any(e.name == base for e in schema.enums)


def _field_is_serializable(type_str: str) -> bool:
    t = type_str.strip()
    if t.startswith('LIST(') or t.startswith('MAP('):
        return False
    return True


def _emit_encode_value(schema: RpcSchema, type_str: str, val_expr: str, out_var: str) -> list[str]:
    """生成编码单个值的代码，追加到 out_var (DataView)"""
    lines = []
    base = _unwrap_type(type_str)
    is_opt = type_str.strip().startswith('OPTIONAL(')
    if is_opt:
        lines.append(f'  if ({val_expr} !== undefined && {val_expr} !== null) {{')
        lines.append(f'    dv.setUint8(off, 1); off += 1;')
        inner_val = f'{val_expr}'
        for line in _emit_encode_value_inner(schema, base, inner_val, 'dv', 'off'):
            lines.append(f'    {line}')
        lines.append(f'  }} else {{')
        lines.append(f'    dv.setUint8(off, 0); off += 1;')
        lines.append(f'  }}')
        return lines
    return _emit_encode_value_inner(schema, base, val_expr, 'dv', 'off', indent=0)


def _emit_encode_value_inner(schema: RpcSchema, base: str, val_expr: str, dv: str, off: str, indent: int = 0) -> list[str]:
    pad = '  ' * indent
    lines = []
    if _c_primitive(base) or _is_enum_type(base, schema):
        if base == 'bool':
            lines.append(f'{pad}{dv}.setUint8({off}, {val_expr} ? 1 : 0); {off} += 1;')
        else:
            lines.append(f'{pad}{dv}.setInt32({off}, {val_expr} | 0, true); {off} += 4;')
    elif base == 'string':
        lines.append(f'{pad}const _s = {val_expr} ?? "";')
        lines.append(f'{pad}const _sb = new TextEncoder().encode(_s);')
        lines.append(f'{pad}{dv}.setUint16({off}, _sb.length, true); {off} += 2;')
        lines.append(f'{pad}new Uint8Array({dv}.buffer).set(_sb, {off}); {off} += _sb.length;')
    else:
        struct = _get_struct(schema, base)
        if struct:
            for f in struct.fields:
                if not f.name:
                    continue
                f_val = f'{val_expr}.{f.name}'
                for line in _emit_encode_value(schema, f.type_str, f_val, ''):
                    lines.append(f'{pad}{line}')
    return lines


def _emit_decode_value(schema: RpcSchema, type_str: str, ret_var: str, dv: str, off: str) -> list[str]:
    """生成解码代码，从 dv[off] 读取，结果赋给 ret_var，推进 off"""
    lines = []
    base = _unwrap_type(type_str)
    is_opt = type_str.strip().startswith('OPTIONAL(')
    if is_opt:
        lines.append(f'  const _present = {dv}.getUint8({off}); {off} += 1;')
        lines.append(f'  if (_present) {{')
        for line in _emit_decode_value_inner(schema, base, ret_var, dv, off, in_opt=True):
            lines.append(f'    {line}')
        lines.append(f'  }} else {{')
        lines.append(f'    {ret_var} = undefined;')
        lines.append(f'  }}')
        return lines
    return _emit_decode_value_inner(schema, base, ret_var, dv, off)


def _emit_decode_value_inner(schema: RpcSchema, base: str, ret_var: str, dv: str, off: str, in_opt: bool = False) -> list[str]:
    lines = []
    if _c_primitive(base) or _is_enum_type(base, schema):
        if base == 'bool':
            lines.append(f'{ret_var} = {dv}.getUint8({off}) !== 0; {off} += 1;')
        else:
            lines.append(f'{ret_var} = {dv}.getInt32({off}, true); {off} += 4;')
    elif base == 'string':
        lines.append(f'const _len = {dv}.getUint16({off}, true); {off} += 2;')
        lines.append(f'{ret_var} = new TextDecoder().decode(new Uint8Array({dv}.buffer, {dv}.byteOffset + {off}, _len)); {off} += _len;')
    else:
        struct = _get_struct(schema, base)
        if struct:
            lines.append(f'{ret_var} = {{}} as any;')
            for f in struct.fields:
                if not f.name:
                    continue
                f_ret = f'{ret_var}.{f.name}'
                for line in _emit_decode_value(schema, f.type_str, f_ret, dv, off):
                    lines.append(line)
    return lines


def _emit_encode_struct(schema: RpcSchema, struct: StructDef, val_expr: str) -> list[str]:
    """编码 struct 到 DataView，返回 ['dv.set...(off, ...); off += ...', ...]"""
    lines = []
    for f in struct.fields:
        if not f.name:
            continue
        f_val = f'{val_expr}.{f.name}'
        for line in _emit_encode_value(schema, f.type_str, f_val, ''):
            lines.append(line)
    return lines


def _emit_decode_struct(schema: RpcSchema, struct: StructDef, ret_var: str) -> list[str]:
    lines = [f'{ret_var} = {{}} as any;']
    for f in struct.fields:
        if not f.name:
            continue
        f_ret = f'{ret_var}.{f.name}'
        for line in _emit_decode_value(schema, f.type_str, f_ret, 'dv', 'off'):
            lines.append(line)
    return lines


def _emit_encode_request_method(schema: RpcSchema, svc: ServiceDef, m: MethodDef, method_id: int) -> list[str]:
    """生成单个方法的请求编码分支"""
    lines = []
    lines.append(f'    if (methodId === {method_id}) {{')
    lines.append(f'      const args = Array.from(argsOrArray);')
    lines.append(f'      let off = 0;')
    # 估算初始大小
    lines.append(f'      let buf = new ArrayBuffer(256);')
    lines.append(f'      let dv = new DataView(buf);')
    lines.append(f'      const ensure = (n: number) => {{')
    lines.append(f'        if (off + n > buf.byteLength) {{')
    lines.append(f'          const newBuf = new ArrayBuffer(buf.byteLength * 2);')
    lines.append(f'          new Uint8Array(newBuf).set(new Uint8Array(buf));')
    lines.append(f'          buf = newBuf; dv = new DataView(buf);')
    lines.append(f'        }}')
    lines.append(f'      }};')
    for i, p in enumerate(m.params):
        base = _unwrap_type(p.type_str)
        arg_expr = f'args[{i}]'
        if _c_primitive(p.type_str):
            if p.type_str.strip() == 'bool':
                lines.append(f'      ensure(1); dv.setUint8(off, {arg_expr} ? 1 : 0); off += 1;')
            else:
                lines.append(f'      ensure(4); dv.setInt32(off, {arg_expr} | 0, true); off += 4;')
        elif p.type_str.strip() == 'bool':
            lines.append(f'      ensure(1); dv.setUint8(off, {arg_expr} ? 1 : 0); off += 1;')
        elif p.type_str.strip().startswith('OPTIONAL(') and _c_primitive(base):
            lines.append(f'      ensure(1);')
            lines.append(f'      if ({arg_expr} !== undefined && {arg_expr} !== null) {{')
            lines.append(f'        dv.setUint8(off, 1); off += 1; ensure(4); dv.setInt32(off, {arg_expr} | 0, true); off += 4;')
            lines.append(f'      }} else {{ dv.setUint8(off, 0); off += 1; }}')
        elif _is_struct_param(p.type_str, schema):
            struct = _get_struct(schema, base)
            if struct:
                for f in struct.fields:
                    if not f.name:
                        continue
                    f_val = f'{arg_expr}.{f.name}'
                    if _is_string_type(f.type_str):
                        is_opt = f.type_str.strip().startswith('OPTIONAL(')
                        if is_opt:
                            lines.append(f'      ensure(1);')
                            lines.append(f'      if ({f_val} !== undefined && {f_val} !== null) {{')
                            lines.append(f'        dv.setUint8(off, 1); off += 1;')
                            lines.append(f'        const _s{f.name} = {f_val}; const _sb{f.name} = new TextEncoder().encode(_s{f.name});')
                            lines.append(f'        ensure(2 + _sb{f.name}.length); dv.setUint16(off, _sb{f.name}.length, true); off += 2;')
                            lines.append(f'        new Uint8Array(buf).set(_sb{f.name}, off); off += _sb{f.name}.length;')
                            lines.append(f'      }} else {{ dv.setUint8(off, 0); off += 1; }}')
                        else:
                            lines.append(f'      const _s{f.name} = {f_val} ?? ""; const _sb{f.name} = new TextEncoder().encode(_s{f.name});')
                            lines.append(f'      ensure(2 + _sb{f.name}.length); dv.setUint16(off, _sb{f.name}.length, true); off += 2;')
                            lines.append(f'      new Uint8Array(buf).set(_sb{f.name}, off); off += _sb{f.name}.length;')
                    elif _c_primitive(_unwrap_type(f.type_str)) or _is_enum_type(f.type_str, schema):
                        is_opt = f.type_str.strip().startswith('OPTIONAL(')
                        if is_opt:
                            lines.append(f'      ensure(1);')
                            lines.append(f'      if ({f_val} !== undefined && {f_val} !== null) {{')
                            lines.append(f'        dv.setUint8(off, 1); off += 1; ensure(4); dv.setInt32(off, {f_val} | 0, true); off += 4;')
                            lines.append(f'      }} else {{ dv.setUint8(off, 0); off += 1; }}')
                        else:
                            lines.append(f'      ensure(4); dv.setInt32(off, {f_val} | 0, true); off += 4;')
    lines.append(f'      return new Uint8Array(buf, 0, off);')
    lines.append(f'    }}')
    return lines


def _emit_decode_response_method(schema: RpcSchema, svc: ServiceDef, m: MethodDef, method_id: int) -> list[str]:
    """生成单个方法的响应解码分支"""
    lines = []
    lines.append(f'    if (methodId === {method_id}) {{')
    lines.append(f'      const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);')
    lines.append(f'      let off = 0;')
    if m.ret_type == 'bool':
        lines.append(f'      return dv.getUint8(off) !== 0;')
    elif m.ret_type.startswith('LIST('):
        elem_type = _unwrap_type(m.ret_type)
        elem_struct = _get_struct(schema, elem_type)
        lines.append(f'      const count = dv.getUint32(off, true); off += 4;')
        lines.append(f'      const items: any[] = [];')
        if elem_struct:
            lines.append(f'      for (let i = 0; i < count; i++) {{')
            lines.append(f'        const item: any = {{}};')
            for f in elem_struct.fields:
                if not f.name or not _field_is_serializable(f.type_str):
                    continue
                f_ret = 'item.' + f.name
                if _is_string_type(f.type_str):
                    is_opt = f.type_str.strip().startswith('OPTIONAL(')
                    if is_opt:
                        lines.append(f'        const _p{f.name} = dv.getUint8(off); off += 1;')
                        lines.append(f'        if (_p{f.name}) {{ const _l{f.name} = dv.getUint16(off, true); off += 2;')
                        lines.append(f'          {f_ret} = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _l{f.name})); off += _l{f.name}; }}')
                    else:
                        lines.append(f'        const _l{f.name} = dv.getUint16(off, true); off += 2;')
                        lines.append(f'        {f_ret} = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _l{f.name})); off += _l{f.name};')
                elif _c_primitive(_unwrap_type(f.type_str)) or _is_enum_type(f.type_str, schema):
                    is_opt = f.type_str.strip().startswith('OPTIONAL(')
                    if is_opt:
                        lines.append(f'        const _p{f.name} = dv.getUint8(off); off += 1;')
                        lines.append(f'        if (_p{f.name}) {{ {f_ret} = dv.getInt32(off, true); off += 4; }}')
                    else:
                        lines.append(f'        {f_ret} = dv.getInt32(off, true); off += 4;')
            lines.append(f'        items.push(item);')
            lines.append(f'      }}')
        lines.append(f'      return {{ items, len: items.length }};')
    else:
        ret_struct = _get_struct(schema, m.ret_type)
        if ret_struct:
            lines.append(f'      const result: any = {{}};')
            for f in ret_struct.fields:
                if not f.name or not _field_is_serializable(f.type_str):
                    continue
                f_ret = 'result.' + f.name
                base = _unwrap_type(f.type_str)
                is_opt = f.type_str.strip().startswith('OPTIONAL(')
                if _is_string_type(f.type_str):
                    if is_opt:
                        lines.append(f'      const _p{f.name} = dv.getUint8(off); off += 1;')
                        lines.append(f'      if (_p{f.name}) {{ const _l{f.name} = dv.getUint16(off, true); off += 2;')
                        lines.append(f'        {f_ret} = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _l{f.name})); off += _l{f.name}; }}')
                    else:
                        lines.append(f'      const _l{f.name} = dv.getUint16(off, true); off += 2;')
                        lines.append(f'      {f_ret} = new TextDecoder().decode(new Uint8Array(payload.buffer, payload.byteOffset + off, _l{f.name})); off += _l{f.name};')
                elif _c_primitive(base) or _is_enum_type(f.type_str, schema):
                    if is_opt:
                        lines.append(f'      const _p{f.name} = dv.getUint8(off); off += 1;')
                        lines.append(f'      if (_p{f.name}) {{ {f_ret} = dv.getInt32(off, true); off += 4; }}')
                    else:
                        lines.append(f'      {f_ret} = dv.getInt32(off, true); off += 4;')
            lines.append(f'      return result;')
        else:
            lines.append(f'      return undefined;')
    lines.append(f'    }}')
    return lines


def emit_binary_codec(schema: RpcSchema, types_path: str = './rpc_types') -> str:
    """生成 rpc_binary_codec.ts"""
    struct_names = [s.name for s in schema.structs]
    lines = [
        "/* Auto-generated - do not edit */",
        "",
    ]
    if struct_names:
        lines.append(f"import type {{ {', '.join(struct_names)} }} from '{types_path}';")
        lines.append("")
    lines.extend([
        "export function encodeRequest(methodId: number, args: IArguments | unknown[]): Uint8Array {",
        "  const argsOrArray = args.length !== undefined ? Array.from(args as IArguments) : (args as unknown[]);",
        "",
    ])
    for svc_idx, svc in enumerate(schema.services):
        for mth_idx, m in enumerate(svc.methods):
            method_id = (svc_idx << 4) | mth_idx
            if m.is_stream:
                lines.append(f'  if (methodId === {method_id}) return new Uint8Array(0);')
            elif not m.params:
                lines.append(f'  if (methodId === {method_id}) return new Uint8Array(0);')
            else:
                lines.extend(_emit_encode_request_method(schema, svc, m, method_id))
    lines.append("  throw new Error(`Unknown methodId: ${methodId}`);")
    lines.append("}")
    lines.append("")
    lines.append("export function decodeResponse<T = unknown>(methodId: number, payload: Uint8Array): T {")
    lines.append("")
    for svc_idx, svc in enumerate(schema.services):
        for mth_idx, m in enumerate(svc.methods):
            method_id = (svc_idx << 4) | mth_idx
            lines.extend(_emit_decode_response_method(schema, svc, m, method_id))
    lines.append("  throw new Error(`Unknown methodId: ${methodId}`);")
    lines.append("}")
    return '\n'.join(lines)


def emit_transport_ws_binary(schema: RpcSchema, codec_path: str = './rpc_binary_codec',
                            default_timeout_ms: int = 2000) -> str:
    """生成使用二进制协议的 transport-ws.ts"""
    return f'''/**
 * WebSocket 传输实现（二进制协议）
 */

import type {{ EsprpcTransport }} from './transport';
import {{ encodeRequest, decodeResponse }} from '{codec_path}';

function closeCodeMessage(code: number): string {{
  const map: Record<number, string> = {{
    1000: '正常关闭',
    1001: '端点离开',
    1002: '协议错误',
    1006: '异常关闭',
    1011: '服务器内部错误',
  }};
  return map[code] ?? `未知错误`;
}}

export function createWebSocketTransport(url: string): EsprpcTransport {{
  let ws: WebSocket | null = null;
  let invokeIdCounter = 1;
  const pending = new Map<number, {{ resolve: (v: unknown) => void; reject: (e: Error) => void; timeoutId: ReturnType<typeof setTimeout> }}>();
  const streamSubs = new Map<number, (data: unknown) => void>();

  return {{
    async call<T = unknown>(methodId: number, args: IArguments, options?: {{ timeout?: number }}): Promise<T> {{
      return new Promise((resolve, reject) => {{
        if (!ws || ws.readyState !== WebSocket.OPEN) {{
          reject(new Error('Not connected'));
          return;
        }}
        const invokeId = invokeIdCounter++;
        if (invokeIdCounter > 0xfffe) invokeIdCounter = 1;
        const timeoutMs = options?.timeout ?? {default_timeout_ms};
        const timeoutId = setTimeout(() => {{
          const h = pending.get(invokeId);
          if (h) {{
            pending.delete(invokeId);
            reject(new Error(`RPC 超时 (${{timeoutMs}}ms)`));
          }}
        }}, timeoutMs);
        pending.set(invokeId, {{
          resolve: (v) => {{ clearTimeout(timeoutId); (resolve as (v: unknown) => void)(v); }},
          reject: (e) => {{ clearTimeout(timeoutId); reject(e); }},
          timeoutId,
        }});
        const payload = encodeRequest(methodId, args);
        const frame = new Uint8Array(5 + payload.length);
        frame[0] = methodId;
        frame[1] = invokeId & 0xff;
        frame[2] = (invokeId >> 8) & 0xff;
        frame[3] = payload.length & 0xff;
        frame[4] = (payload.length >> 8) & 0xff;
        frame.set(payload, 5);
        ws.send(frame);
      }});
    }},
    sendStreamRequest(methodId: number, args?: IArguments | unknown[]): void {{
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      const payload = encodeRequest(methodId, args ?? {{ length: 0 }} as IArguments);
      const frame = new Uint8Array(5 + payload.length);
      frame[0] = methodId;
      frame[1] = 0;
      frame[2] = 0;
      frame[3] = payload.length & 0xff;
      frame[4] = (payload.length >> 8) & 0xff;
      frame.set(payload, 5);
      ws.send(frame);
    }},
    subscribe<T = unknown>(methodId: number, cb: (data: T) => void): void {{
      streamSubs.set(methodId, cb as (data: unknown) => void);
    }},
    unsubscribe(methodId: number): void {{
      streamSubs.delete(methodId);
    }},
    async connect(): Promise<void> {{
      ws = new WebSocket(url);
      await new Promise<void>((resolve, reject) => {{
        if (!ws) return reject(new Error('No socket'));
        let settled = false;
        const settle = (err?: Error) => {{
          if (settled) return;
          settled = true;
          if (err) reject(err);
          else resolve();
        }};
        ws.onopen = () => settle();
        let errorFallback: ReturnType<typeof setTimeout> | null = null;
        ws.onerror = () => {{
          if (!settled) {{
            errorFallback = setTimeout(() => {{
              if (!settled) settle(new Error(`连接失败: ${{url}}`));
            }}, 100);
          }}
        }};
        ws.onclose = (ev) => {{
          if (errorFallback) clearTimeout(errorFallback);
          if (settled) return;
          settle(new Error(`连接失败: ${{ev.reason || closeCodeMessage(ev.code)}} (code ${{ev.code}})`));
        }};
      }});
      ws.binaryType = 'arraybuffer';
      ws.onmessage = (ev) => {{
        try {{
          const data = new Uint8Array(ev.data as ArrayBuffer);
          if (data.length < 5) return;
          const methodId = data[0];
          const invokeId = data[1] | (data[2] << 8);
          const payloadLen = data[3] | (data[4] << 8);
          if (data.length < 5 + payloadLen) return;
          const payload = data.subarray(5, 5 + payloadLen);
          const result = decodeResponse(methodId, payload);
          if (invokeId !== 0) {{
            const h = pending.get(invokeId);
            if (h) {{
              pending.delete(invokeId);
              h.resolve(result);
            }}
          }} else {{
            const cb = streamSubs.get(methodId);
            if (cb) cb(result);
          }}
        }} catch (_) {{}}
      }};
    }},
    disconnect(): void {{
      if (ws) {{ ws.close(); ws = null; }}
      pending.forEach((h) => {{ clearTimeout(h.timeoutId); h.reject(new Error('Disconnected')); }});
      pending.clear();
    }},
  }};
}}
'''
