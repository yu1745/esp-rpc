#!/usr/bin/env python3
"""
RPC 代码生成器 - 解析 .rpc.hpp 生成 TS stub 和 C++ 端序列化/分发代码
用法:
  完整生成（TS + C++）: python main.py -o <output_dir> [options] <file1.rpc.hpp> ...
  仅 C++ 生成:          python main.py [options] <file1.rpc.hpp> ...
"""

import argparse
import os
import shutil
import sys

# 支持直接运行或作为模块
_gen_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _gen_dir)
from parser import parse_file, RpcSchema  # noqa: E402
from ts_emitter import emit_all  # noqa: E402
from ts_binary_emitter import emit_binary_codec, emit_transport_ws_binary, emit_transport_ble_binary, emit_transport_serial_binary  # noqa: E402
from c_emitter import emit_cpp_gen_header, emit_cpp_gen_impl, emit_c_service_impl_user  # noqa: E402


def merge_schemas(schemas: list[RpcSchema]) -> RpcSchema:
    """合并多个文件的 schema"""
    merged = RpcSchema()
    seen_enums = set()
    seen_structs = set()
    seen_services = set()
    for s in schemas:
        for e in s.enums:
            if e.name not in seen_enums:
                merged.enums.append(e)
                seen_enums.add(e.name)
        for st in s.structs:
            if st.name not in seen_structs:
                merged.structs.append(st)
                seen_structs.add(st.name)
        for svc in s.services:
            if svc.name not in seen_services:
                merged.services.append(svc)
                seen_services.add(svc.name)
    return merged


def main():
    parser = argparse.ArgumentParser(description='RPC code generator')
    parser.add_argument('-o', '--output', default=None,
                       help='Output directory for TS stubs; omit for C++-only generation')
    parser.add_argument('-s', '--transport-src', default=None,
                       help='Directory with transport.ts, transport-ws.ts to copy (default: ts/src)')
    parser.add_argument('-t', '--timeout', type=int, default=2000,
                       help='RPC call default timeout in ms (default: 2000)')
    parser.add_argument('files', nargs='+', help='.rpc.hpp files to process')
    args = parser.parse_args()

    ts_output = args.output is not None
    if ts_output:
        output_dir = args.output
        os.makedirs(output_dir, exist_ok=True)
        # 复制 transport.ts 接口定义；transport-ws.ts 由生成器产出（二进制协议）
        _project_root = os.path.dirname(_gen_dir)
        transport_src = args.transport_src or os.path.join(_project_root, 'ts', 'src')
        for name in ('transport.ts',):
            src = os.path.join(transport_src, name)
            if os.path.exists(src):
                shutil.copy2(src, os.path.join(output_dir, name))
                print(f'Copied {name} -> {output_dir}')
            else:
                print(f'Warning: {src} not found, skipping', file=sys.stderr)

    schemas = []
    for f in args.files:
        if not os.path.exists(f):
            print(f'Warning: {f} not found', file=sys.stderr)
            continue
        try:
            schema = parse_file(f)
            schemas.append((f, schema))
        except Exception as e:
            print(f'Error parsing {f}: {e}', file=sys.stderr)
            raise

    if not schemas:
        print('No valid .rpc.hpp files', file=sys.stderr)
        sys.exit(1)

    merged = merge_schemas([s for _, s in schemas])

    if ts_output:
        # 使用同目录导入 ./transport
        types_content, client_content = emit_all(merged, transport_path='./transport')

        types_path = os.path.join(output_dir, 'rpc_types.ts')
        client_path = os.path.join(output_dir, 'rpc_client.ts')

        with open(types_path, 'w', encoding='utf-8') as f:
            f.write(types_content)
        print(f'Generated {types_path}')

        with open(client_path, 'w', encoding='utf-8') as f:
            f.write(client_content)
        print(f'Generated {client_path}')

        # 生成二进制编解码与 transport-ws（与 C 端二进制协议一致）
        codec_content = emit_binary_codec(merged, types_path='./rpc_types')
        codec_path = os.path.join(output_dir, 'rpc_binary_codec.ts')
        with open(codec_path, 'w', encoding='utf-8') as f:
            f.write(codec_content)
        print(f'Generated {codec_path}')
        transport_ws_content = emit_transport_ws_binary(merged, codec_path='./rpc_binary_codec',
                                                       default_timeout_ms=args.timeout)
        transport_ws_path = os.path.join(output_dir, 'transport-ws.ts')
        with open(transport_ws_path, 'w', encoding='utf-8') as f:
            f.write(transport_ws_content)
        print(f'Generated {transport_ws_path}')

        transport_ble_content = emit_transport_ble_binary(merged, codec_path='./rpc_binary_codec',
                                                          default_timeout_ms=args.timeout)
        transport_ble_path = os.path.join(output_dir, 'transport-ble.ts')
        with open(transport_ble_path, 'w', encoding='utf-8') as f:
            f.write(transport_ble_content)
        print(f'Generated {transport_ble_path}')

        transport_serial_content = emit_transport_serial_binary(merged, codec_path='./rpc_binary_codec',
                                                               default_timeout_ms=args.timeout)
        transport_serial_path = os.path.join(output_dir, 'transport-serial.ts')
        with open(transport_serial_path, 'w', encoding='utf-8') as f:
            f.write(transport_serial_content)
        print(f'Generated {transport_serial_path}')

    # 为每个 .rpc.hpp 生成 C++ 合并文件（1h + 1cpp + impl_user.cpp）
    for rpc_h_path, schema in schemas:
        if not schema.services:
            continue
        rpc_h_basename = os.path.basename(rpc_h_path)
        out_dir = os.path.dirname(rpc_h_path)
        base = rpc_h_basename.replace('.rpc.hpp', '')

        # 合并头文件 .rpc.gen.hpp
        h_content = emit_cpp_gen_header(schema, rpc_h_basename)
        h_path = os.path.join(out_dir, f'{base}.rpc.gen.hpp')
        with open(h_path, 'w', encoding='utf-8') as f:
            f.write(h_content)
        print(f'Generated {h_path}')

        # 合并实现 .rpc.gen.cpp
        cpp_content = emit_cpp_gen_impl(schema, rpc_h_basename)
        cpp_path = os.path.join(out_dir, f'{base}.rpc.gen.cpp')
        with open(cpp_path, 'w', encoding='utf-8') as f:
            f.write(cpp_content)
        print(f'Generated {cpp_path}')

        # 用户实现 .rpc.impl_user.cpp（仅不存在时生成）
        impl_user_path = os.path.join(out_dir, f'{base}.rpc.impl_user.cpp')
        impl_user_content = emit_c_service_impl_user(schema, rpc_h_basename)
        if impl_user_content and not os.path.exists(impl_user_path):
            with open(impl_user_path, 'w', encoding='utf-8') as f:
                f.write(impl_user_content)
            print(f'Created {impl_user_path} (template, will not overwrite)')

        # 删除旧格式生成文件
        for old in [f'{base}.rpc.c', f'{base}.rpc.dispatch.h', f'{base}.rpc.impl.c', f'{base}.rpc.impl.h', f'{base}.rpc.gen.h']:
            old_path = os.path.join(out_dir, old)
            if os.path.exists(old_path):
                os.remove(old_path)
                print(f'Removed {old_path}')


if __name__ == '__main__':
    main()
