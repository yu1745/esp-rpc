"""
C 端 dispatch 代码生成 - 解析二进制 payload，调用服务实现，序列化二进制响应
帧格式: [1B method_id][2B invoke_id LE][2B payload_len LE][binary payload]
"""

try:
    from .parser import RpcSchema, ServiceDef, MethodDef, StructDef, StructField
except ImportError:
    from parser import RpcSchema, ServiceDef, MethodDef, StructDef, StructField


def _c_primitive(type_str: str) -> bool:
    t = type_str.strip()
    return t in ('int', 'int32', 'int64', 'uint32', 'uint64', 'bool', 'float', 'double')


def _get_struct(schema: RpcSchema, name: str) -> StructDef | None:
    """根据名称查找 struct 定义"""
    for s in schema.structs:
        if s.name == name:
            return s
    return None


def _unwrap_type(type_str: str) -> str:
    """REQUIRED(string) -> string, OPTIONAL(int) -> int, 原始类型不变"""
    t = type_str.strip()
    if t.startswith('REQUIRED(') and t.endswith(')'):
        return t[9:-1].strip()
    if t.startswith('OPTIONAL(') and t.endswith(')'):
        return t[9:-1].strip()
    if t.startswith('LIST(') and t.endswith(')'):
        return t[5:-1].strip()
    return t


def _is_string_type(type_str: str) -> bool:
    """是否为 string 类型（含 REQUIRED(string), OPTIONAL(string)）"""
    base = _unwrap_type(type_str)
    return base == 'string'


def _is_struct_param(type_str: str, schema: RpcSchema) -> bool:
    """是否为 struct 类型参数（非 primitive、非 LIST/STREAM）"""
    t = type_str.strip()
    if t.startswith('LIST(') or t.startswith('STREAM(') or t.startswith('OPTIONAL('):
        inner = _unwrap_type(t)
        if _c_primitive(inner) or inner == 'string':
            return False
        return _get_struct(schema, inner) is not None
    return _get_struct(schema, t) is not None


def _is_enum_type(type_str: str, schema: RpcSchema) -> bool:
    """是否为 enum 类型"""
    base = _unwrap_type(type_str)
    return any(e.name == base for e in schema.enums)


def _emit_parse_struct_bin(schema: RpcSchema, struct: StructDef) -> str:
    """生成 struct 的二进制解析函数，从 (*p, end) 读取"""
    fn = f'bin_read_{struct.name}'
    lines = [
        f'static int {fn}(const uint8_t **p, const uint8_t *end, {struct.name} *out) {{',
        f'    memset(out, 0, sizeof(*out));',
    ]
    for f in struct.fields:
        if not f.name:
            continue
        base = _unwrap_type(f.type_str)
        is_opt = f.type_str.strip().startswith('OPTIONAL(')
        if _is_string_type(f.type_str):
            lines.append(f'    {{')
            lines.append(f'        static char {f.name}_buf[128];')
            if is_opt:
                lines.append(f'        bool {f.name}_present = false;')
                lines.append(f'        if (esprpc_bin_read_optional_tag(p, end, &{f.name}_present) != 0) return -1;')
                lines.append(f'        if ({f.name}_present) {{')
                lines.append(f'            if (esprpc_bin_read_str(p, end, {f.name}_buf, sizeof({f.name}_buf)) != 0) return -1;')
                lines.append(f'            out->{f.name}.present = true; out->{f.name}.value = {f.name}_buf;')
                lines.append(f'        }} else {{ out->{f.name}.present = false; }}')
            else:
                lines.append(f'        if (esprpc_bin_read_str(p, end, {f.name}_buf, sizeof({f.name}_buf)) != 0) return -1;')
                lines.append(f'        out->{f.name} = {f.name}_buf;')
            lines.append(f'    }}')
        elif _c_primitive(base) or _is_enum_type(f.type_str, schema):
            if is_opt:
                lines.append(f'    {{')
                lines.append(f'        bool {f.name}_present = false;')
                lines.append(f'        if (esprpc_bin_read_optional_tag(p, end, &{f.name}_present) != 0) return -1;')
                lines.append(f'        if ({f.name}_present) {{')
                lines.append(f'            int v = 0;')
                lines.append(f'            if (esprpc_bin_read_i32(p, end, &v) != 0) return -1;')
                lines.append(f'            out->{f.name}.present = true; out->{f.name}.value = v;')
                lines.append(f'        }} else {{ out->{f.name}.present = false; }}')
                lines.append(f'    }}')
            else:
                lines.append(f'    if (esprpc_bin_read_i32(p, end, &out->{f.name}) != 0) return -1;')
        elif f.type_str.strip().startswith('LIST('):
            # LIST(T): [4B count][elem0][elem1]...
            elem_type = base
            elem_struct = _get_struct(schema, elem_type)
            if _is_string_type(f.type_str):
                lines.append(f'    {{')
                lines.append(f'        uint32_t {f.name}_count = 0;')
                lines.append(f'        if (esprpc_bin_read_u32(p, end, &{f.name}_count) != 0) return -1;')
                lines.append(f'        #define {f.name.upper()}_MAX 8')
                lines.append(f'        static char {f.name}_buf[{f.name.upper()}_MAX][64];')
                lines.append(f'        static char *{f.name}_ptrs[{f.name.upper()}_MAX];')
                lines.append(f'        size_t {f.name}_n = ({f.name}_count < {f.name.upper()}_MAX) ? {f.name}_count : {f.name.upper()}_MAX;')
                lines.append(f'        for (size_t i = 0; i < {f.name}_n; i++) {{')
                lines.append(f'            if (esprpc_bin_read_str(p, end, {f.name}_buf[i], sizeof({f.name}_buf[i])) != 0) return -1;')
                lines.append(f'            {f.name}_ptrs[i] = {f.name}_buf[i];')
                lines.append(f'        }}')
                lines.append(f'        out->{f.name}.items = {f.name}_ptrs;')
                lines.append(f'        out->{f.name}.len = {f.name}_n;')
                lines.append(f'        for (size_t i = {f.name}_n; i < {f.name}_count; i++) {{')
                lines.append(f'            if (esprpc_bin_read_str(p, end, {f.name}_buf[0], sizeof({f.name}_buf[0])) != 0) return -1;')
                lines.append(f'        }}')
                lines.append(f'        #undef {f.name.upper()}_MAX')
                lines.append(f'    }}')
            elif elem_struct:
                lines.append(f'    {{')
                lines.append(f'        uint32_t {f.name}_count = 0;')
                lines.append(f'        if (esprpc_bin_read_u32(p, end, &{f.name}_count) != 0) return -1;')
                lines.append(f'        #define {f.name.upper()}_MAX 8')
                lines.append(f'        static {elem_struct.name} {f.name}_arr[{f.name.upper()}_MAX];')
                lines.append(f'        size_t {f.name}_n = ({f.name}_count < {f.name.upper()}_MAX) ? {f.name}_count : {f.name.upper()}_MAX;')
                lines.append(f'        for (size_t i = 0; i < {f.name}_n; i++) {{')
                lines.append(f'            if (bin_read_{elem_struct.name}(p, end, &{f.name}_arr[i]) != 0) return -1;')
                lines.append(f'        }}')
                lines.append(f'        out->{f.name}.items = {f.name}_arr;')
                lines.append(f'        out->{f.name}.len = {f.name}_n;')
                lines.append(f'        for (size_t i = {f.name}_n; i < {f.name}_count; i++) {{')
                lines.append(f'            {elem_struct.name} _skip;')
                lines.append(f'            if (bin_read_{elem_struct.name}(p, end, &_skip) != 0) return -1;')
                lines.append(f'        }}')
                lines.append(f'        #undef {f.name.upper()}_MAX')
                lines.append(f'    }}')
            else:
                elem_c = _type_str_to_c(elem_type)
                lines.append(f'    {{')
                lines.append(f'        uint32_t {f.name}_count = 0;')
                lines.append(f'        if (esprpc_bin_read_u32(p, end, &{f.name}_count) != 0) return -1;')
                lines.append(f'        #define {f.name.upper()}_MAX 8')
                lines.append(f'        static {elem_c} {f.name}_arr[{f.name.upper()}_MAX];')
                lines.append(f'        size_t {f.name}_n = ({f.name}_count < {f.name.upper()}_MAX) ? {f.name}_count : {f.name.upper()}_MAX;')
                lines.append(f'        for (size_t i = 0; i < {f.name}_n; i++) {{')
                lines.append(f'            if (esprpc_bin_read_i32(p, end, (int *)&{f.name}_arr[i]) != 0) return -1;')
                lines.append(f'        }}')
                lines.append(f'        out->{f.name}.items = {f.name}_arr;')
                lines.append(f'        out->{f.name}.len = {f.name}_n;')
                lines.append(f'        for (size_t i = {f.name}_n; i < {f.name}_count; i++) {{')
                lines.append(f'            int _skip;')
                lines.append(f'            if (esprpc_bin_read_i32(p, end, &_skip) != 0) return -1;')
                lines.append(f'        }}')
                lines.append(f'        #undef {f.name.upper()}_MAX')
                lines.append(f'    }}')
        else:
            lines.append(f'    if (esprpc_bin_read_i32(p, end, (int *)&out->{f.name}) != 0) return -1;')
    lines.append(f'    return 0;')
    lines.append(f'}}')
    return '\n'.join(lines)


def _emit_serialize_struct_bin(schema: RpcSchema, struct: StructDef, var_name: str = 'r', skip_complex: bool = False) -> list[str]:
    """生成结构体二进制序列化代码块，写入到 (*wp, end)。skip_complex 仅用于兼容，LIST 已支持"""
    lines = []
    for f in struct.fields:
        if not f.name:
            continue
        base = _unwrap_type(f.type_str)
        is_opt = f.type_str.strip().startswith('OPTIONAL(')
        if f.type_str.strip().startswith('LIST('):
            # LIST(T): [4B count][elem0][elem1]...
            elem_type = base
            elem_struct = _get_struct(schema, elem_type)
            if _is_string_type(f.type_str):
                lines.append(f'    if (esprpc_bin_write_u32(&wp, wend, (uint32_t)({var_name}.{f.name}.len)) != 0) return -1;')
                lines.append(f'    if ({var_name}.{f.name}.items && {var_name}.{f.name}.len > 0) {{')
                lines.append(f'        for (size_t j = 0; j < {var_name}.{f.name}.len; j++) {{')
                lines.append(f'            if (esprpc_bin_write_str(&wp, wend, {var_name}.{f.name}.items[j] ? {var_name}.{f.name}.items[j] : "") != 0) return -1;')
                lines.append(f'        }}')
                lines.append(f'    }}')
            elif elem_struct:
                lines.append(f'    if (esprpc_bin_write_u32(&wp, wend, (uint32_t)({var_name}.{f.name}.len)) != 0) return -1;')
                lines.append(f'    if ({var_name}.{f.name}.items && {var_name}.{f.name}.len > 0) {{')
                lines.append(f'        for (size_t j = 0; j < {var_name}.{f.name}.len; j++) {{')
                for line in _emit_serialize_struct_bin(schema, elem_struct, f'{var_name}.{f.name}.items[j]', skip_complex=True):
                    lines.append(f'            {line}')
                lines.append(f'        }}')
                lines.append(f'    }}')
            else:
                # LIST(primitive)
                lines.append(f'    if (esprpc_bin_write_u32(&wp, wend, (uint32_t)({var_name}.{f.name}.len)) != 0) return -1;')
                lines.append(f'    if ({var_name}.{f.name}.items && {var_name}.{f.name}.len > 0) {{')
                lines.append(f'        for (size_t j = 0; j < {var_name}.{f.name}.len; j++) {{')
                lines.append(f'            if (esprpc_bin_write_i32(&wp, wend, (int){var_name}.{f.name}.items[j]) != 0) return -1;')
                lines.append(f'        }}')
                lines.append(f'    }}')
        elif _is_string_type(f.type_str):
            if is_opt:
                lines.append(f'    if (esprpc_bin_write_optional_tag(&wp, wend, {var_name}.{f.name}.present) != 0) return -1;')
                lines.append(f'    if ({var_name}.{f.name}.present && esprpc_bin_write_str(&wp, wend, {var_name}.{f.name}.value) != 0) return -1;')
            else:
                lines.append(f'    if (esprpc_bin_write_str(&wp, wend, {var_name}.{f.name} ? {var_name}.{f.name} : "") != 0) return -1;')
        elif _c_primitive(base) or _is_enum_type(f.type_str, schema):
            if is_opt:
                lines.append(f'    if (esprpc_bin_write_optional_tag(&wp, wend, {var_name}.{f.name}.present) != 0) return -1;')
                lines.append(f'    if ({var_name}.{f.name}.present && esprpc_bin_write_i32(&wp, wend, {var_name}.{f.name}.value) != 0) return -1;')
            else:
                lines.append(f'    if (esprpc_bin_write_i32(&wp, wend, {var_name}.{f.name}) != 0) return -1;')
        else:
            lines.append(f'    if (esprpc_bin_write_i32(&wp, wend, (int){var_name}.{f.name}) != 0) return -1;')
    return lines


def _field_is_serializable(type_str: str) -> bool:
    """字段是否可简单序列化（LIST 已支持，MAP 已移除）"""
    return True


def _emit_method_dispatch(schema: RpcSchema, svc: ServiceDef, m: MethodDef, method_idx: int) -> list[str]:
    """为单个方法生成 dispatch 分支（二进制协议）"""
    lines = []
    # 1. 参数解析（从 p 顺序读取）
    lines.append(f'        const uint8_t *p = req_buf;')
    lines.append(f'        const uint8_t *end = req_buf + req_len;')
    call_args = []
    for p in m.params:
        base = _unwrap_type(p.type_str)
        is_opt = p.type_str.strip().startswith('OPTIONAL(')
        c_type = _type_str_to_c(p.type_str)
        if _c_primitive(p.type_str):
            lines.append(f'        int {p.name}_val = 0;')
            lines.append(f'        if (esprpc_bin_read_i32((const uint8_t **)&p, end, &{p.name}_val) != 0) return -1;')
            call_args.append(f'{p.name}_val')
        elif p.type_str.strip() == 'bool':
            lines.append(f'        bool {p.name}_val = false;')
            lines.append(f'        if (esprpc_bin_read_bool((const uint8_t **)&p, end, &{p.name}_val) != 0) return -1;')
            call_args.append(f'{p.name}_val')
        elif is_opt and _c_primitive(base):
            lines.append(f'        {c_type} {p.name} = {{ false, 0 }};')
            lines.append(f'        {{ bool pr = false; if (esprpc_bin_read_optional_tag((const uint8_t **)&p, end, &pr) != 0) return -1;')
            lines.append(f'          if (pr) {{ int v = 0; if (esprpc_bin_read_i32((const uint8_t **)&p, end, &v) != 0) return -1;')
            lines.append(f'            {p.name}.present = true; {p.name}.value = v; }} }}')
            call_args.append(p.name)
        elif _is_struct_param(p.type_str, schema):
            struct = _get_struct(schema, base)
            if struct:
                lines.append(f'        {c_type} {p.name} = {{}};')
                lines.append(f'        if (bin_read_{struct.name}((const uint8_t **)&p, end, &{p.name}) != 0) return -1;')
                call_args.append(p.name)
            else:
                lines.append(f'        {c_type} {p.name} = {{}};')
                call_args.append(p.name)
        else:
            lines.append(f'        {c_type} {p.name} = {{}};')
            call_args.append(p.name)

    # 2. 调用服务
    args_str = ', '.join(call_args)
    ret_c = _type_str_to_c(m.ret_type)
    lines.append(f'        {ret_c} r = svc->{m.name}({args_str});')

    # 3. 响应序列化（二进制）
    lines.append(f'        *resp_len = 1024;')
    lines.append(f'        *resp_buf = (uint8_t *)malloc(*resp_len);')
    lines.append(f'        if (!*resp_buf) return -1;')
    lines.append(f'        uint8_t *wp = *resp_buf;')
    lines.append(f'        const uint8_t *wend = *resp_buf + *resp_len;')

    if m.ret_type == 'bool':
        lines.append(f'        if (esprpc_bin_write_bool(&wp, wend, r) != 0) {{ free(*resp_buf); *resp_buf = NULL; return -1; }}')
        lines.append(f'        *resp_len = (size_t)(wp - *resp_buf);')
        lines.append(f'        return 0;')
    elif m.ret_type.startswith('LIST('):
        elem_type = _unwrap_type(m.ret_type)
        elem_struct = _get_struct(schema, elem_type)
        lines.append(f'        if (esprpc_bin_write_u32(&wp, wend, (uint32_t)(r.len)) != 0) {{ free(*resp_buf); *resp_buf = NULL; return -1; }}')
        lines.append(f'        if (r.items && r.len > 0) {{')
        if elem_struct:
            lines.append(f'            for (size_t i = 0; i < r.len; i++) {{')
            for line in _emit_serialize_struct_bin(schema, elem_struct, 'r.items[i]', skip_complex=True):
                lines.append(f'                {line}')
            lines.append(f'            }}')
        lines.append(f'        }}')
        lines.append(f'        *resp_len = (size_t)(wp - *resp_buf);')
        lines.append(f'        return 0;')
    else:
        ret_struct = _get_struct(schema, m.ret_type)
        if ret_struct:
            for line in _emit_serialize_struct_bin(schema, ret_struct, 'r'):
                lines.append(f'        {line}')
        lines.append(f'        *resp_len = (size_t)(wp - *resp_buf);')
        lines.append(f'        return 0;')

    return lines


def _emit_stream_dispatch(schema: RpcSchema, svc: ServiceDef, m: MethodDef, method_idx: int, full_method_id: int) -> list[str]:
    """为 stream 方法生成 dispatch 分支"""
    lines = []
    lines.append(f'        const uint8_t *p = req_buf;')
    lines.append(f'        const uint8_t *end = req_buf + req_len;')
    call_args = []
    for p in m.params:
        base = _unwrap_type(p.type_str)
        is_opt = p.type_str.strip().startswith('OPTIONAL(')
        c_type = _type_str_to_c(p.type_str)
        if _c_primitive(p.type_str):
            lines.append(f'        int {p.name}_val = 0;')
            lines.append(f'        if (esprpc_bin_read_i32((const uint8_t **)&p, end, &{p.name}_val) != 0) return -1;')
            call_args.append(f'{p.name}_val')
        elif p.type_str.strip() == 'bool':
            lines.append(f'        bool {p.name}_val = false;')
            lines.append(f'        if (esprpc_bin_read_bool((const uint8_t **)&p, end, &{p.name}_val) != 0) return -1;')
            call_args.append(f'{p.name}_val')
        elif is_opt and _c_primitive(base):
            lines.append(f'        {c_type} {p.name} = {{ false, 0 }};')
            lines.append(f'        {{ bool pr = false; if (esprpc_bin_read_optional_tag((const uint8_t **)&p, end, &pr) != 0) return -1;')
            lines.append(f'          if (pr) {{ int v = 0; if (esprpc_bin_read_i32((const uint8_t **)&p, end, &v) != 0) return -1;')
            lines.append(f'            {p.name}.present = true; {p.name}.value = v; }} }}')
            call_args.append(p.name)
        elif _is_struct_param(p.type_str, schema):
            struct = _get_struct(schema, base)
            if struct:
                lines.append(f'        {c_type} {p.name} = {{}};')
                lines.append(f'        if (bin_read_{struct.name}((const uint8_t **)&p, end, &{p.name}) != 0) return -1;')
                call_args.append(p.name)
            else:
                lines.append(f'        {c_type} {p.name} = {{}};')
                call_args.append(p.name)
        else:
            lines.append(f'        {c_type} {p.name} = {{}};')
            call_args.append(p.name)

    lines.append(f'        esprpc_set_stream_method_id(method_id);')
    args_str = ', '.join(call_args)
    ret_c = f'rpc_stream<{m.ret_type}>'
    lines.append(f'        {ret_c} r = svc->{m.name}({args_str});')
    lines.append(f'        esprpc_set_stream_method_id(ESPRPC_STREAM_METHOD_ID_NONE);')
    lines.append(f'        (void)r;')
    lines.append(f'        *resp_buf = NULL;')
    lines.append(f'        *resp_len = 0;')
    lines.append(f'        return 0;')
    return lines


def _emit_bin_dispatch(schema: RpcSchema, svc: ServiceDef) -> str:
    """生成二进制 payload 的 dispatch 函数"""
    lines = [
        f'int {svc.name}_dispatch(uint16_t method_id, const uint8_t *req_buf, size_t req_len,',
        f'                      uint8_t **resp_buf, size_t *resp_len, void *svc_ctx) {{',
        f'    {svc.name} *svc = ({svc.name} *)svc_ctx;',
        f'    uint8_t mth = method_id & 0x0F;',
        f'',
    ]

    for i, m in enumerate(svc.methods):
        if m.is_stream:
            lines.append(f'    if (mth == {i}) {{')
            lines.extend(_emit_stream_dispatch(schema, svc, m, i, (0 << 4) | i))
            lines.append(f'    }}')
            lines.append(f'')
        else:
            lines.append(f'    if (mth == {i}) {{')
            lines.extend(_emit_method_dispatch(schema, svc, m, i))
            lines.append(f'    }}')
            lines.append(f'')

    lines.append(f'    return -1;')
    lines.append(f'}}')
    return '\n'.join(lines)


def emit_dispatch_header(schema: RpcSchema, rpc_h_basename: str) -> str:
    """生成 dispatch 函数声明头文件"""
    guard = rpc_h_basename.replace('.', '_').upper().replace('_RPC_H', '_RPC_DISPATCH_H')
    lines = [
        f'/* Auto-generated - do not edit */',
        f'#ifndef {guard}',
        f'#define {guard}',
        f'#include <stdint.h>',
        f'#include <stddef.h>',
        f'#include <stdbool.h>',
        f'',
    ]
    for svc in schema.services:
        lines.append(f'int {svc.name}_dispatch(uint16_t method_id, const uint8_t *req_buf, size_t req_len,')
        lines.append(f'                      uint8_t **resp_buf, size_t *resp_len, void *svc_ctx);')
        lines.append(f'')
    lines.append(f'#endif')
    return '\n'.join(lines)


def _type_str_to_c(type_str: str) -> str:
    """将解析器的 type_str 转为 C 类型名"""
    t = type_str.strip()
    if t.startswith('OPTIONAL(') and t.endswith(')'):
        inner = t[9:-1].strip()
        return f'{inner}_optional'
    if t.startswith('LIST(') and t.endswith(')'):
        inner = t[5:-1].strip()
        return f'{inner}_list'
    if t.startswith('STREAM(') and t.endswith(')'):
        inner = t[7:-1].strip()
        return f'rpc_stream<{inner}>'
    return t


def _method_to_snake(name: str) -> str:
    """GetUser -> get_user, CreateUser -> create_user"""
    s = []
    for i, c in enumerate(name):
        if c.isupper() and i > 0:
            s.append('_')
        s.append(c.lower())
    return ''.join(s)


def _default_return_expr(ret_type: str, is_stream: bool = False) -> str:
    """根据返回类型生成占位返回值表达式"""
    if is_stream:
        c_type = f'rpc_stream<{ret_type}>'
        return f'return ({c_type}){{ nullptr }};'
    c_type = _type_str_to_c(ret_type)
    if c_type == 'bool':
        return 'return false;'
    if c_type.endswith('_list'):
        return f'return ({c_type}){{ nullptr, 0 }};'
    if 'rpc_stream<' in c_type:
        return f'return ({c_type}){{ nullptr }};'
    return f'return ({c_type}){{}};'


def _emit_impl_extern_and_vtable(svc: ServiceDef) -> str:
    """生成 impl：仅 extern 声明 + vtable 组装，不含函数体"""
    lines = [
        f'/* {svc.name} - 仅 vtable 组装，实现请在 impl_user.cpp 中编写 */',
        f'',
    ]
    for m in svc.methods:
        if m.params:
            params_str = ', '.join(f'{_type_str_to_c(p.type_str)} {p.name}' for p in m.params)
        else:
            params_str = 'void'
        ret_c = f'rpc_stream<{m.ret_type}>' if m.is_stream else _type_str_to_c(m.ret_type)
        fn_name = f'{_method_to_snake(m.name)}_impl'
        lines.append(f'extern {ret_c} {fn_name}({params_str});')
    lines.append(f'')
    var_name = f'{_method_to_snake(svc.name)}_impl_instance'
    lines.append(f'{svc.name} {var_name} = {{')
    for m in svc.methods:
        fn_name = f'{_method_to_snake(m.name)}_impl'
        lines.append(f'    {fn_name},')
    lines.append(f'}};')
    return '\n'.join(lines)


def _emit_service_impl_user_stubs(svc: ServiceDef) -> str:
    """生成 impl_user.cpp：用户可编辑的实现占位"""
    lines = [
        f'/* {svc.name} 实现 - 此文件由用户编辑，不会被生成器覆盖 */',
        f'',
    ]
    for m in svc.methods:
        if m.params:
            params_str = ', '.join(f'{_type_str_to_c(p.type_str)} {p.name}' for p in m.params)
        else:
            params_str = 'void'
        ret_c = f'rpc_stream<{m.ret_type}>' if m.is_stream else _type_str_to_c(m.ret_type)
        fn_name = f'{_method_to_snake(m.name)}_impl'
        lines.append(f'{ret_c} {fn_name}({params_str})')
        lines.append(f'{{')
        for p in m.params:
            lines.append(f'    (void){p.name};')
        lines.append(f'    // TODO: 实现业务逻辑')
        lines.append(f'    {_default_return_expr(m.ret_type, m.is_stream)}')
        lines.append(f'}}')
        lines.append(f'')
    return '\n'.join(lines)


def emit_c_service_impl(schema: RpcSchema, rpc_h_basename: str) -> tuple[str, str]:
    """生成服务实现 impl.c 和 impl.h"""
    rpc_base = rpc_h_basename.replace('.rpc.hpp', '')
    if not schema.services:
        return '', ''

    impl_c_lines = [
        f'/* Auto-generated - do not edit */',
        f'#include "{rpc_h_basename}"',
        f'',
    ]
    impl_h_lines = [
        f'/* Auto-generated - do not edit */',
        f'#ifndef {rpc_base.upper().replace("-", "_")}_RPC_IMPL_H',
        f'#define {rpc_base.upper().replace("-", "_")}_RPC_IMPL_H',
        f'#include "{rpc_h_basename}"',
        f'',
    ]
    for svc in schema.services:
        impl_c_lines.append(_emit_impl_extern_and_vtable(svc))
        var_name = f'{_method_to_snake(svc.name)}_impl_instance'
        impl_h_lines.append(f'extern {svc.name} {var_name};')
        impl_h_lines.append(f'')
    impl_h_lines.append(f'#endif')
    return '\n'.join(impl_c_lines), '\n'.join(impl_h_lines)


def emit_c_service_impl_user(schema: RpcSchema, rpc_h_basename: str) -> str:
    """生成 impl_user.cpp 内容（用户实现占位）"""
    rpc_base = rpc_h_basename.replace('.rpc.hpp', '')
    if not schema.services:
        return ''

    lines = [
        f'/* 用户实现 - 此文件不会被生成器覆盖，请在此编写业务逻辑 */',
        f'#include "{rpc_h_basename.replace(".rpc.hpp", ".rpc.gen.hpp")}"',
        f'#include "esprpc_binary.h"',
        f'',
    ]
    for svc in schema.services:
        lines.append(_emit_service_impl_user_stubs(svc))
    return '\n'.join(lines)


def _collect_struct_params(schema: RpcSchema) -> set[str]:
    """收集所有作为方法参数的 struct 类型名"""
    needed = set()
    for svc in schema.services:
        for m in svc.methods:
            if m.is_stream:
                continue
            for p in m.params:
                if _is_struct_param(p.type_str, schema):
                    base = _unwrap_type(p.type_str)
                    if _get_struct(schema, base):
                        needed.add(base)
    return needed


def emit_c_dispatch(schema: RpcSchema, rpc_h_basename: str) -> str:
    """为 schema 中的每个 service 生成 C dispatch，返回完整文件内容（二进制协议）
    序列化/反序列化逻辑复用 esprpc_binary.c，生成代码仅含调用逻辑"""
    lines = [
        f'/* Auto-generated - do not edit */',
        f'#include "{rpc_h_basename}"',
        f'#include "esprpc.h"',
        f'#include "esprpc_service.h"',
        f'#include "esprpc_binary.h"',
        f'#include <stdlib.h>',
        f'#include <string.h>',
        f'',
    ]
    # 动态生成各 struct 的解析函数
    for struct_name in sorted(_collect_struct_params(schema)):
        struct = _get_struct(schema, struct_name)
        if struct:
            lines.append(_emit_parse_struct_bin(schema, struct))
            lines.append('')
    for svc in schema.services:
        lines.append(_emit_bin_dispatch(schema, svc))
    return '\n'.join(lines)


def emit_cpp_gen_header(schema: RpcSchema, rpc_h_basename: str) -> str:
    """生成合并头文件 .rpc.gen.hpp：dispatch 声明 + impl_instance 声明"""
    rpc_base = rpc_h_basename.replace('.rpc.hpp', '')
    guard = f'{rpc_base.upper().replace("-", "_")}_RPC_GEN_HPP'
    lines = [
        '/* Auto-generated - do not edit */',
        f'#ifndef {guard}',
        f'#define {guard}',
        f'#include "{rpc_h_basename}"',
        f'#include <cstdint>',
        f'#include <cstddef>',
        f'',
        f'#ifdef __cplusplus',
        f'extern "C" {{',
        f'#endif',
        f'',
    ]
    for svc in schema.services:
        lines.append(f'int {svc.name}_dispatch(uint16_t method_id, const uint8_t *req_buf, size_t req_len,')
        lines.append(f'                      uint8_t **resp_buf, size_t *resp_len, void *svc_ctx);')
        lines.append(f'')
        var_name = f'{_method_to_snake(svc.name)}_impl_instance'
        lines.append(f'extern {svc.name} {var_name};')
        lines.append(f'')
    lines.append(f'#ifdef __cplusplus')
    lines.append(f'}}')
    lines.append(f'#endif')
    lines.append(f'')
    lines.append(f'#endif')
    return '\n'.join(lines)


def emit_cpp_gen_impl(schema: RpcSchema, rpc_h_basename: str) -> str:
    """生成合并实现 .rpc.gen.cpp：dispatch + vtable（extern impl 在 impl_user.cpp）"""
    lines = [
        '/* Auto-generated - do not edit */',
        f'#include "{rpc_h_basename.replace(".rpc.hpp", ".rpc.gen.hpp")}"',
        f'#include "esprpc.h"',
        f'#include "esprpc_service.h"',
        f'#include "esprpc_binary.h"',
        f'#include <cstdlib>',
        f'#include <cstring>',
        f'',
    ]
    for struct_name in sorted(_collect_struct_params(schema)):
        struct = _get_struct(schema, struct_name)
        if struct:
            lines.append(_emit_parse_struct_bin(schema, struct))
            lines.append('')
    for svc in schema.services:
        lines.append(_emit_impl_extern_and_vtable(svc))
        lines.append(_emit_bin_dispatch(schema, svc))
    return '\n'.join(lines)
