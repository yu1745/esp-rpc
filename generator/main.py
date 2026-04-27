#!/usr/bin/env python3
"""
ESP-RPC TypeScript binding generator.

Parses C++ headers with ESPRPC_STRUCT/ESPRPC_SERVICE macros and @rpc annotations,
generates rpc_types.ts and rpc_client.ts.

Usage:
  python3 generator/main.py -o <output_dir> path/to/service.hpp [path/to/another.hpp ...]
"""

import re
import os
import sys
import argparse
from dataclasses import dataclass, field
from pathlib import Path


# ---------------------------------------------------------------------------
# Data models
# ---------------------------------------------------------------------------

@dataclass
class StructField:
    name: str
    cpp_type: str

@dataclass
class RpcStruct:
    name: str
    fields: list[StructField]

@dataclass
class MethodParam:
    name: str
    cpp_type: str

@dataclass
class RpcMethod:
    name: str
    params: list[MethodParam]
    return_type: str
    method_id: int
    flags: str  # "" | "MF_VOID" | "MF_STREAM"

@dataclass
class RpcService:
    name: str
    methods: list[RpcMethod]


# ---------------------------------------------------------------------------
# Macro body extractor (handles nested parens)
# ---------------------------------------------------------------------------

def extract_macro_call(text: str, macro_name: str) -> list[tuple[str, str]]:
    """Find all `MACRO_NAME(...)` calls, return [(name, body)] where body
    is the content between outer parens, respecting nesting."""
    results = []
    pattern = re.compile(rf'{macro_name}\s*\(')
    for m in pattern.finditer(text):
        start = m.end()
        depth = 1
        i = start
        while i < len(text) and depth > 0:
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
            i += 1
        if depth == 0:
            body = text[start:i-1]
            results.append((m.group(), body))
    return results


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def parse_header(filepath: str) -> tuple[list[RpcStruct], list[RpcService]]:
    with open(filepath, 'r', encoding='utf-8') as f:
        text = f.read()

    structs = _parse_structs(text)
    services = _parse_services(text, structs)
    return structs, services


def _parse_structs(text: str) -> list[RpcStruct]:
    structs = []
    for _, body in extract_macro_call(text, 'ESPRPC_STRUCT'):
        # First token is the struct name
        body = body.strip()
        name_end = body.index(',')
        name = body[:name_end].strip()
        fields_body = body[name_end+1:].strip()
        fields = _parse_struct_fields(fields_body)
        structs.append(RpcStruct(name, fields))
    return structs


def _parse_struct_fields(body: str) -> list[StructField]:
    fields = []
    depth = 0
    cur = ''
    for ch in body:
        if ch == '(':
            if depth > 0:
                cur += ch
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                cur = cur.strip()
                parts = [p.strip() for p in cur.split(',', 1)]
                if len(parts) == 2:
                    fields.append(StructField(parts[0], parts[1]))
                cur = ''
            else:
                cur += ch
        elif ch == ',' and depth == 0:
            pass
        else:
            if depth > 0:
                cur += ch
    return fields


def _parse_services(text: str, structs: list[RpcStruct]) -> list[RpcService]:
    services = []
    for _, body in extract_macro_call(text, 'ESPRPC_SERVICE'):
        body = body.strip()
        # First two args: name, impl
        # Need to split carefully, tracking paren depth
        args = _split_top_level_commas(body)
        if len(args) < 3:
            continue
        svc_name = args[0].strip()
        methods_body = ','.join(args[2:])

        rpc_annotations = _parse_rpc_annotations(methods_body)

        method_pattern = re.compile(
            r'esprpc::method\s*<\s*\w+\s*,\s*&\s*\w+\s*::\s*(\w+)\s*>\s*\(\s*(\d+)\s*(?:,\s*(MF_\w+))?\s*\)',
            re.DOTALL
        )
        methods = []
        for mm in method_pattern.finditer(methods_body):
            mth_name = mm.group(1)
            mth_id = int(mm.group(2))
            mth_flags = mm.group(3) or ''

            annotation = rpc_annotations.get(mth_name)
            params = []
            return_type = 'void'
            if annotation:
                params = annotation['params']
                return_type = annotation['return_type']

            methods.append(RpcMethod(mth_name, params, return_type, mth_id, mth_flags))

        if methods:
            services.append(RpcService(svc_name, methods))

    return services


def _split_top_level_commas(s: str) -> list[str]:
    """Split by commas at depth 0 (not inside parens)."""
    parts = []
    depth = 0
    cur = ''
    for ch in s:
        if ch == '(':
            depth += 1
            cur += ch
        elif ch == ')':
            depth -= 1
            cur += ch
        elif ch == ',' and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


def _parse_rpc_annotations(text: str) -> dict[str, dict]:
    annotations = {}
    pattern = re.compile(
        r'//\s*@rpc\s+(\w+)\s*\(([^)]*)\)\s*->\s*(\S+)',
        re.DOTALL
    )
    for m in pattern.finditer(text):
        name = m.group(1)
        params_str = m.group(2).strip()
        return_type = m.group(3).strip()

        params = []
        if params_str:
            for p in re.finditer(r'([^\s,]+)\s+(\w+)(?:\s*,|\s*$)', params_str):
                ptype = p.group(1).strip()
                pname = p.group(2).strip()
                params.append(MethodParam(pname, ptype))

        annotations[name] = {'params': params, 'return_type': return_type}

    return annotations


# ---------------------------------------------------------------------------
# Type helpers
# ---------------------------------------------------------------------------

def strip_namespace(t: str) -> str:
    return t.replace('esprpc::', '').replace('rpc_', '').strip()


def unwrap_optional(t: str) -> str | None:
    m = re.match(r'(?:esprpc::)?Optional\s*<\s*(.+?)\s*>', t)
    return m.group(1) if m else None


def unwrap_list(t: str) -> str | None:
    m = re.match(r'(?:esprpc::)?List\s*<\s*(.+?)\s*>', t)
    return m.group(1) if m else None


def unwrap_stream(t: str) -> str | None:
    m = re.match(r'(?:rpc_)?stream\s*<\s*(.+?)\s*>', t)
    return m.group(1) if m else None


PRIMITIVE_TYPES = {'int32_t', 'int', 'uint32_t', 'int16_t', 'uint16_t', 'uint8_t', 'bool'}
STRING_TYPES = {'StringBuf', 'string', 'char', 'char*'}


def is_primitive(t: str) -> bool:
    return t in PRIMITIVE_TYPES


def is_string(t: str) -> bool:
    return t in STRING_TYPES


def ts_type(cpp_type: str) -> str:
    t = strip_namespace(cpp_type)
    if t in PRIMITIVE_TYPES:
        return 'number' if t != 'bool' else 'boolean'
    if is_string(t):
        return 'string'
    inner = unwrap_optional(t)
    if inner:
        return f'{ts_type(inner)} | undefined'
    inner = unwrap_list(t)
    if inner:
        return f'{ts_type(inner)}[]'
    return t


def ts_type_for_return(cpp_type: str) -> str:
    t = strip_namespace(cpp_type)
    inner = unwrap_stream(t)
    if inner:
        return ts_type(inner)
    inner = unwrap_list(t)
    if inner:
        return f'{ts_type(inner)}[]'
    if t in PRIMITIVE_TYPES:
        return 'number' if t != 'bool' else 'boolean'
    return t


# ---------------------------------------------------------------------------
# Serialization codegen
# ---------------------------------------------------------------------------

SER_WRITE = {
    'int32_t': '.writeI32', 'int': '.writeI32', 'uint32_t': '.writeU32',
    'int16_t': '.writeU16', 'uint16_t': '.writeU16', 'uint8_t': '.writeU8',
    'bool': '.writeBool',
}
SER_READ = {
    'int32_t': '.readI32()', 'int': '.readI32()', 'uint32_t': '.readU32()',
    'int16_t': '.readU16()', 'uint16_t': '.readU16()', 'uint8_t': '.readU8()',
    'bool': '.readBool()',
}


def gen_write(field: StructField, indent: str) -> str:
    t = field.cpp_type.strip()
    name = field.name
    inner_opt = unwrap_optional(t)
    if inner_opt:
        inner = gen_write(StructField(name, inner_opt), indent + '  ')
        return f'{indent}w.writeBool({name} !== undefined);\n{indent}if ({name} !== undefined) {{\n{inner}{indent}}}'
    inner_list = unwrap_list(t)
    if inner_list:
        inner = gen_write(StructField('item', inner_list), indent + '  ')
        return f'{indent}w.writeU32({name}.length);\n{indent}for (const item of {name}) {{\n{inner}{indent}}}'
    t_clean = strip_namespace(t)
    if is_primitive(t_clean):
        return f'{indent}w{SER_WRITE[t_clean]}({name});'
    if is_string(t_clean):
        return f'{indent}w.writeStr({name});'
    return f'{indent}write{t_clean}(w, {name});'


def gen_read(field: StructField, indent: str) -> str:
    t = field.cpp_type.strip()
    name = field.name
    inner_opt = unwrap_optional(t)
    if inner_opt:
        inner = gen_read(StructField(name, inner_opt), indent)
        return f'{indent}r.readBool() ? ({inner.strip()}) : undefined'
    inner_list = unwrap_list(t)
    if inner_list:
        inner = gen_read(StructField('item', inner_list), indent)
        r = f'{indent}(() => {{\n'
        r += f'{indent}  const _c = r.readU32();\n'
        r += f'{indent}  const _a: typeof({unnest_for_read(inner_list)})[] = [];\n'
        r += f'{indent}  for (let _i = 0; _i < _c; _i++) _a.push({inner.strip()});\n'
        r += f'{indent}  return _a;\n'
        r += f'{indent}}})()'
        return r
    t_clean = strip_namespace(t)
    if is_primitive(t_clean):
        return f'{indent}r{SER_READ[t_clean]}'
    if is_string(t_clean):
        return f'{indent}r.readStr()'
    return f'{indent}read{t_clean}(r)'


def unnest_for_read(cpp_type: str) -> str:
    """Get the item type expression for use in typeof(expr) array."""
    t = strip_namespace(cpp_type)
    inner_list = unwrap_list(t)
    if inner_list:
        return f'{unnest_for_read(inner_list)}[]'
    if is_primitive(t):
        return 'number' if t != 'bool' else 'boolean'
    if is_string(t):
        return 'string'
    inner_opt = unwrap_optional(t)
    if inner_opt:
        return f'{unnest_for_read(inner_opt)} | undefined'
    return t


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------

def generate_types(structs: list[RpcStruct]) -> str:
    lines = []
    lines.append('// Auto-generated by esp-rpc generator - do not edit\n')

    # Interfaces
    for s in structs:
        lines.append(f'export interface {s.name} {{')
        for f in s.fields:
            lines.append(f'  {f.name}: {ts_type(f.cpp_type)};')
        lines.append('}\n')

    # Serialize / deserialize functions
    for s in structs:
        sn = s.name
        lines.append(f'export function write{sn}(w: BufferWriter, v: {sn}): void {{')
        for f in s.fields:
            lines.append(gen_write(f, '  '))
        lines.append('}\n')

        lines.append(f'export function read{sn}(r: BufferReader): {sn} {{')
        lines.append('  return {')
        for i, f in enumerate(s.fields):
            val = gen_read(f, '    ')
            comma = ',' if i < len(s.fields) - 1 else ','
            lines.append(f'    {f.name}: {val}{comma}')
        lines.append('  };')
        lines.append('}\n')

    return '\n'.join(lines) + '\n'


def generate_client(svc: RpcService, structs: list[RpcStruct]) -> str:
    lines = []
    lines.append('// Auto-generated by esp-rpc generator - do not edit\n')
    lines.append("import { BufferWriter, BufferReader } from './binary';")
    lines.append("import type { EsprpcTransport } from './transport';")

    type_refs = set()
    for m in svc.methods:
        ret = m.return_type
        if ret != 'void':
            ret_clean = strip_namespace(ret)
            inner_stream = unwrap_stream(ret_clean)
            inner_list = unwrap_list(ret_clean)
            if inner_stream:
                type_refs.add(inner_stream)
            elif inner_list:
                type_refs.add(strip_namespace(inner_list))
            elif ret_clean not in PRIMITIVE_TYPES and not is_string(ret_clean):
                type_refs.add(ret_clean)
        for p in m.params:
            t = strip_namespace(p.cpp_type)
            inner_opt = unwrap_optional(t)
            inner_list = unwrap_list(t)
            if inner_opt:
                t = strip_namespace(inner_opt)
            if inner_list:
                t = strip_namespace(inner_list)
            if t not in PRIMITIVE_TYPES and not is_string(t):
                type_refs.add(t)

    if type_refs:
        lines.append(f"import type {{ {', '.join(sorted(type_refs))} }} from './rpc_types';")
    lines.append('')

    svc_name_clean = svc.name.replace('Service', '')
    lines.append(f'export class {svc.name}Client {{')
    lines.append('  readonly #transport: EsprpcTransport;')
    lines.append(f'  constructor(transport: EsprpcTransport) {{')
    lines.append('    this.#transport = transport;')
    lines.append('  }\n')

    for m in svc.methods:
        _gen_method(lines, m)
        lines.append('')

    lines.append('}\n')
    return '\n'.join(lines) + '\n'


def _gen_method(lines: list[str], m: RpcMethod):
    is_void = m.flags == 'MF_VOID'
    is_stream = m.flags == 'MF_STREAM'

    params_str = ', '.join(f'{p.name}: {ts_type(p.cpp_type)}' for p in m.params)

    if is_stream:
        inner = unwrap_stream(m.return_type) or m.return_type
        inner_ts = ts_type(inner)
        lines.append(f'  {m.name}(): {{ subscribe(cb: (v: {inner_ts}) => void): () => void }} {{')
        lines.append('    return {')
        lines.append(f'      subscribe: (cb) => {{')
        lines.append(f'        this.#transport.subscribe({m.method_id}, (data) => {{')
        lines.append(f'          const r = new BufferReader(data);')
        lines.append(f'          cb(read{strip_namespace(inner)}(r));')
        lines.append('        });')
        lines.append(f'        return () => this.#transport.unsubscribe({m.method_id});')
        lines.append('      },')
        lines.append('    };')
        lines.append('  }')
        return

    if is_void:
        lines.append(f'  {m.name}({params_str}): void {{')
    else:
        ret_ts = ts_type_for_return(m.return_type)
        lines.append(f'  async {m.name}({params_str}): Promise<{ret_ts}> {{')

    has_list_return = 'List<' in m.return_type

    if m.params:
        lines.append('    const w = new BufferWriter();')
        for p in m.params:
            inner_opt = unwrap_optional(p.cpp_type)
            if inner_opt:
                lines.append(f'    w.writeBool({p.name} !== undefined);')
                lines.append(f'    if ({p.name} !== undefined) {{')
                pf = StructField('inner', inner_opt)
                inner_code = gen_write(pf, '      ')
                lines.append(inner_code)
                lines.append('    }')
            else:
                pf = StructField(p.name, p.cpp_type)
                lines.append(gen_write(pf, '    '))

        timeout_arg = ', { timeout: 5000 }' if has_list_return else ''
        lines.append(f'    const resp = await this.#transport.call({m.method_id}, w.toBytes(){timeout_arg});')
    else:
        timeout_arg = ', { timeout: 5000 }' if has_list_return else ''
        lines.append(f'    const resp = await this.#transport.call({m.method_id}, new Uint8Array(0){timeout_arg});')

    if is_void:
        lines.append('  }')
        return

    ret_clean = strip_namespace(m.return_type)
    inner_list = unwrap_list(ret_clean)
    if inner_list:
        item_ts = ts_type(inner_list)
        lines.append('    const r = new BufferReader(resp);')
        lines.append('    const _count = r.readU32();')
        lines.append(f'    const _items: {item_ts}[] = [];')
        lines.append(f'    for (let _i = 0; _i < _count; _i++) _items.push(read{strip_namespace(inner_list)}(r));')
        lines.append('    return _items;')
    elif ret_clean in PRIMITIVE_TYPES:
        lines.append('    const r = new BufferReader(resp);')
        lines.append(f'    return r{SER_READ[ret_clean]};')
    elif is_string(ret_clean):
        lines.append('    const r = new BufferReader(resp);')
        lines.append('    return r.readStr();')
    else:
        lines.append('    const r = new BufferReader(resp);')
        lines.append(f'    return read{ret_clean}(r);')

    lines.append('  }')


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='ESP-RPC TS binding generator')
    parser.add_argument('-o', '--output', required=True, help='Output directory')
    parser.add_argument('inputs', nargs='+', help='Input .hpp files')
    args = parser.parse_args()

    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    all_structs: list[RpcStruct] = []
    all_services: list[RpcService] = []

    for input_path in args.inputs:
        structs, services = parse_header(input_path)
        all_structs.extend(structs)
        all_services.extend(services)

    types_content = generate_types(all_structs)
    (output_dir / 'rpc_types.ts').write_text(types_content, encoding='utf-8')
    print(f'  Generated {output_dir / "rpc_types.ts"}')

    for svc in all_services:
        client_content = generate_client(svc, all_structs)
        svc_lower = svc.name[0].lower() + svc.name[1:]
        (output_dir / f'{svc_lower}_client.ts').write_text(client_content, encoding='utf-8')
        print(f'  Generated {output_dir / f"{svc_lower}_client.ts"}')


if __name__ == '__main__':
    main()
