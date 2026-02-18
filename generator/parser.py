"""
RPC 宏解析器 - 解析 .rpc.hpp 文件中的 RPC_SERVICE, RPC_METHOD, RPC_STRUCT, RPC_ENUM 等
"""

import re
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class EnumValue:
    name: str
    value: Optional[str] = None


@dataclass
class EnumDef:
    name: str
    values: list[EnumValue]


@dataclass
class StructField:
    type_str: str  # 原始类型字符串，如 "int", "REQUIRED(string)", "OPTIONAL(int) page"
    name: str


@dataclass
class StructDef:
    name: str
    fields: list[StructField]


@dataclass
class MethodParam:
    type_str: str
    name: str


@dataclass
class MethodDef:
    name: str
    ret_type: str
    params: list[MethodParam]
    options: Optional[str] = None  # RPC_METHOD_EX 的 options 字符串
    is_stream: bool = False


@dataclass
class ServiceDef:
    name: str
    methods: list[MethodDef]


@dataclass
class RpcSchema:
    enums: list[EnumDef] = field(default_factory=list)
    structs: list[StructDef] = field(default_factory=list)
    services: list[ServiceDef] = field(default_factory=list)


def _parse_enum(content: str) -> Optional[EnumDef]:
    """解析 RPC_ENUM(name, ...)"""
    m = re.search(r'RPC_ENUM\s*\(\s*(\w+)\s*,\s*(.+?)\s*\)', content, re.DOTALL)
    if not m:
        return None
    name = m.group(1)
    body = m.group(2).strip()
    values = []
    for part in re.split(r',\s*(?![^()]*\))', body):
        part = part.strip()
        if '=' in part:
            n, v = part.split('=', 1)
            values.append(EnumValue(n.strip(), v.strip()))
        else:
            values.append(EnumValue(part))
    return EnumDef(name=name, values=values)


def _find_matching_paren(s: str, start: int) -> int:
    """从 start 位置（应在 '(' 之后）找到匹配的 ')' 位置"""
    depth = 1
    i = start
    while i < len(s) and depth > 0:
        if s[i] == '(':
            depth += 1
        elif s[i] == ')':
            depth -= 1
        i += 1
    return i - 1 if depth == 0 else -1


def _parse_struct(content: str) -> Optional[StructDef]:
    """解析 RPC_STRUCT(name, field1; field2; ...)"""
    m = re.search(r'RPC_STRUCT\s*\(\s*(\w+)\s*,\s*', content)
    if not m:
        return None
    name = m.group(1)
    # 从 RPC_STRUCT 后的 '(' 开始找匹配的 ')'
    paren_start = content.find('(', m.start())
    end = _find_matching_paren(content, paren_start + 1)
    if end < 0:
        return None
    inner = content[paren_start + 1:end].strip()
    # 格式为 "name, field1; field2; ..."，取第一个逗号后的内容
    comma = inner.find(',')
    body = inner[comma + 1:].strip() if comma >= 0 else inner
    fields = []
    for line in body.split(';'):
        line = line.strip().rstrip(';')
        if not line:
            continue
        # 类型可能含括号如 OPTIONAL(string), LIST(string)，最后一词为字段名
        mf = re.match(r'^(.+?)\s+(\w+)\s*$', line)
        if mf:
            fields.append(StructField(type_str=mf.group(1).strip(), name=mf.group(2)))
        else:
            fields.append(StructField(type_str=line, name=''))
    return StructDef(name=name, fields=fields)


def _parse_method_params(params_str: str) -> list[MethodParam]:
    """解析方法参数，如 'int id' 或 'CreateUserRequest request'"""
    params = []
    for part in re.split(r',\s*(?![^<>]*>)', params_str):
        part = part.strip()
        if not part or part == 'void':
            continue
        tokens = part.split()
        if len(tokens) >= 2:
            params.append(MethodParam(type_str=tokens[0], name=tokens[-1]))
        elif len(tokens) == 1:
            params.append(MethodParam(type_str=tokens[0], name='arg'))
    return params


def _parse_service(content: str) -> Optional[ServiceDef]:
    """解析 RPC_SERVICE(name) ... RPC_SERVICE_END(name)，方法顺序与 .hpp 声明一致"""
    m = re.search(r'RPC_SERVICE\s*\(\s*(\w+)\s*\)\s*(.+?)RPC_SERVICE_END\s*\(\s*\w+\s*\)', content, re.DOTALL)
    if not m:
        return None
    name = m.group(1)
    body = m.group(2).strip()
    # 收集 (start_pos, MethodDef)，最后按 start_pos 排序以保持与 struct 成员顺序一致
    ordered: list[tuple[int, MethodDef]] = []

    # RPC_METHOD(name, STREAM(Type), params)
    for mth in re.finditer(r'RPC_METHOD\s*\(\s*(\w+)\s*,\s*STREAM\s*\(\s*(\w+)\s*\)\s*,\s*(.*?)\s*\)', body, re.DOTALL):
        ordered.append((mth.start(), MethodDef(
            name=mth.group(1),
            ret_type=mth.group(2),
            params=_parse_method_params(mth.group(3)),
            is_stream=True
        )))

    # RPC_METHOD(name, ret_type, params) - 非 STREAM
    for mth in re.finditer(r'RPC_METHOD\s*\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(.*?)\s*\)', body, re.DOTALL):
        if mth.group(2) == 'STREAM':
            continue  # 已由上面处理
        ordered.append((mth.start(), MethodDef(
            name=mth.group(1),
            ret_type=mth.group(2),
            params=_parse_method_params(mth.group(3)),
            is_stream=False
        )))

    # RPC_METHOD_EX(name, ret_type, params, "options")
    for mth in re.finditer(r'RPC_METHOD_EX\s*\(\s*(\w+)\s*,\s*(\S+)\s*,\s*(.+?)\s*,\s*"([^"]*)"\s*\)', body, re.DOTALL):
        params_str = mth.group(3).strip()
        ordered.append((mth.start(), MethodDef(
            name=mth.group(1),
            ret_type=mth.group(2),
            params=_parse_method_params(params_str),
            options=mth.group(4),
            is_stream=False
        )))

    ordered.sort(key=lambda x: x[0])
    methods = [defn for _, defn in ordered]
    return ServiceDef(name=name, methods=methods)


def parse_file(path: str) -> RpcSchema:
    """解析 .rpc.hpp 文件，返回 RpcSchema"""
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    schema = RpcSchema()

    # 移除单行注释和块注释
    content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)

    for enum in re.finditer(r'RPC_ENUM\s*\([^)]+\)[^;]*', content):
        e = _parse_enum(enum.group(0))
        if e:
            schema.enums.append(e)

    for m in re.finditer(r'RPC_STRUCT\s*\(\s*\w+\s*,\s*', content):
        s = _parse_struct(content[m.start():])
        if s:
            schema.structs.append(s)

    for svc in re.finditer(r'RPC_SERVICE\s*\([^)]+\)\s*.*?RPC_SERVICE_END\s*\(\s*\w+\s*\)', content, re.DOTALL):
        s = _parse_service(svc.group(0))
        if s:
            schema.services.append(s)

    return schema


def parse_content(content: str) -> RpcSchema:
    """解析字符串内容"""
    import tempfile
    with tempfile.NamedTemporaryFile(mode='w', suffix='.rpc.hpp', delete=False) as f:
        f.write(content)
        f.flush()
        try:
            return parse_file(f.name)
        finally:
            import os
            os.unlink(f.name)
