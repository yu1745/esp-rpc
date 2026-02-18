"""
TypeScript 代码生成器
"""

try:
    from .parser import RpcSchema, EnumDef, StructDef, ServiceDef, MethodDef, StructField
except ImportError:
    from parser import RpcSchema, EnumDef, StructDef, ServiceDef, MethodDef, StructField


def _extract_custom_type_names(type_str: str) -> set[str]:
    """从类型字符串中提取自定义类型名（用于按需导入）"""
    builtin = {'int', 'int32', 'int64', 'uint32', 'uint64', 'bool', 'float', 'double', 'string'}
    t = type_str.strip()
    if t in builtin:
        return set()
    if t.startswith('OPTIONAL('):
        return _extract_custom_type_names(t[9:-1])
    if t.startswith('LIST('):
        return _extract_custom_type_names(t[5:-1])
    if t.startswith('REQUIRED('):
        return _extract_custom_type_names(t[9:-1])
    return {t}


def c_type_to_ts(c_type: str) -> str:
    """C 类型映射到 TypeScript"""
    t = c_type.strip()
    if t in ('int', 'int32', 'int64', 'uint32', 'uint64', 'float', 'double'):
        return 'number'
    if t == 'bool':
        return 'boolean'
    if t == 'string':
        return 'string'
    if t.startswith('OPTIONAL('):
        inner = t[9:-1].strip()
        return f'{c_type_to_ts(inner)} | undefined'
    if t.startswith('LIST('):
        inner = t[5:-1].strip()
        return f'{c_type_to_ts(inner)}[]'
    if t.startswith('REQUIRED('):
        inner = t[9:-1].strip()
        return c_type_to_ts(inner)
    return t  # 自定义类型如 User, UserResponse


def emit_enum(e: EnumDef) -> str:
    """生成 const 对象以兼容 erasableSyntaxOnly"""
    lines = [f'export const {e.name} = {{']
    for v in e.values:
        if v.value:
            lines.append(f'  {v.name}: {v.value},')
        else:
            lines.append(f'  {v.name}: 0,')
    lines.append('} as const;')
    lines.append(f'export type {e.name} = typeof {e.name}[keyof typeof {e.name}];')
    return '\n'.join(lines)


def emit_struct_field(f: StructField) -> str:
    ts_type = c_type_to_ts(f.type_str)
    optional = '?' if 'OPTIONAL' in f.type_str or 'undefined' in ts_type else ''
    return f'  {f.name}{optional}: {ts_type};'


def emit_struct(s: StructDef) -> str:
    lines = [f'export interface {s.name} {{']
    for f in s.fields:
        if f.name:
            lines.append(emit_struct_field(f))
    lines.append('}')
    return '\n'.join(lines)


def emit_method_ret_type(m: MethodDef) -> str:
    if m.is_stream:
        return f'{{ subscribe(cb: (v: {m.ret_type}) => void): () => void }}'
    return f'Promise<{c_type_to_ts(m.ret_type)}>'


def emit_method_params(m: MethodDef) -> str:
    if not m.params:
        return ''
    parts = []
    for i, p in enumerate(m.params):
        ts_type = c_type_to_ts(p.type_str)
        name = f'_{p.name}' if p.name and p.name != 'arg' else f'_arg{i}'
        parts.append(f'{name}: {ts_type}')
    return ', '.join(parts)


def emit_service(svc: ServiceDef, transport_var: str = 'transport', include_import: bool = True, transport_path: str = '../src/transport', svc_idx: int = 0) -> str:
    lines = []
    if include_import:
        lines.extend([
            f"import type {{ EsprpcTransport }} from '{transport_path}';",
            '',
        ])
    lines.extend([
        f'export class {svc.name}Client {{',
        f'  readonly #{transport_var}: EsprpcTransport;',
        f'  constructor({transport_var}: EsprpcTransport) {{',
        f'    this.#{transport_var} = {transport_var};',
        f'  }}',
        ''
    ])

    for mth_idx, m in enumerate(svc.methods):
        method_id = (svc_idx << 4) | mth_idx
        ret_ts = c_type_to_ts(m.ret_type) if not m.is_stream else m.ret_type
        if m.is_stream:
            lines.append(f'  {m.name}(): {emit_method_ret_type(m)} {{')
            lines.append(f'    return {{ subscribe: (cb) => {{')
            lines.append(f'      this.#{transport_var}.subscribe<{ret_ts}>({method_id}, cb);')
            lines.append(f'      this.#{transport_var}.sendStreamRequest({method_id}, []);')
            lines.append(f'      return () => this.#{transport_var}.unsubscribe({method_id});')
            lines.append(f'    }} }};')
            lines.append(f'  }}')
        else:
            params = emit_method_params(m)
            opts = ', { timeout: 5000 }' if m.options and 'timeout' in str(m.options) else ''
            lines.append(f'  async {m.name}({params}): {emit_method_ret_type(m)} {{')
            lines.append(f'    return this.#{transport_var}.call<{ret_ts}>({method_id}, arguments{opts});')
            lines.append(f'  }}')
        lines.append('')

    lines.append('}')
    return '\n'.join(lines)


def emit_all(schema: RpcSchema, base_name: str = 'rpc', transport_path: str = '../src/transport') -> tuple[str, str]:
    """
    生成 TS 代码，返回 (types_content, client_content)
    """
    types_lines = [
        "/* Auto-generated - do not edit */",
        "",
    ]
    type_names = set()
    for e in schema.enums:
        types_lines.append(emit_enum(e))
        types_lines.append('')
    for s in schema.structs:
        types_lines.append(emit_struct(s))
        types_lines.append('')

    all_type_names = set(e.name for e in schema.enums) | set(s.name for s in schema.structs)
    used_types = set()
    for svc in schema.services:
        for m in svc.methods:
            used_types.update(_extract_custom_type_names(m.ret_type))
            for p in m.params:
                used_types.update(_extract_custom_type_names(p.type_str))
    used_types &= all_type_names
    import_types = sorted(used_types) if used_types else sorted(all_type_names)
    client_lines = [
        "/* Auto-generated - do not edit */",
        "",
        f"import type {{ {', '.join(import_types)} }} from './rpc_types';",
        f"import type {{ EsprpcTransport }} from '{transport_path}';",
        "",
    ]
    for i, svc in enumerate[ServiceDef](schema.services):
        client_lines.append(emit_service(svc, transport_path=transport_path, include_import=False, svc_idx=i))
        client_lines.append('')

    return ('\n'.join(types_lines), '\n'.join(client_lines))
