#!/usr/bin/env python3
"""
RPC 代码生成器 - 解析 .rpc.h 生成 TS stub 和 C 端序列化/分发代码
用法: python main.py -o <output_dir> <file1.rpc.h> [file2.rpc.h ...]
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
from ts_binary_emitter import emit_binary_codec, emit_transport_ws_binary  # noqa: E402
from c_emitter import emit_c_dispatch, emit_dispatch_header, emit_c_service_impl, emit_c_service_impl_user  # noqa: E402


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
    parser.add_argument('-o', '--output', required=True, help='Output directory for generated files')
    parser.add_argument('-s', '--transport-src', default=None,
                       help='Directory with transport.ts, transport-ws.ts to copy (default: ts/src)')
    parser.add_argument('-t', '--timeout', type=int, default=2000,
                       help='RPC call default timeout in ms (default: 2000)')
    parser.add_argument('files', nargs='+', help='.rpc.h files to process')
    args = parser.parse_args()

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
        print('No valid .rpc.h files', file=sys.stderr)
        sys.exit(1)

    merged = merge_schemas([s for _, s in schemas])

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

    # 为每个 .rpc.h 生成 C dispatch 及声明头文件（输出到同目录）
    for rpc_h_path, schema in schemas:
        if not schema.services:
            continue
        rpc_h_basename = os.path.basename(rpc_h_path)
        out_dir = os.path.dirname(rpc_h_path)
        base = rpc_h_basename.replace('.rpc.h', '')
        c_content = emit_c_dispatch(schema, rpc_h_basename)
        c_path = os.path.join(out_dir, f'{base}.rpc.c')
        with open(c_path, 'w', encoding='utf-8') as f:
            f.write(c_content)
        print(f'Generated {c_path}')
        h_content = emit_dispatch_header(schema, rpc_h_basename)
        h_path = os.path.join(out_dir, f'{base}.rpc.dispatch.h')
        with open(h_path, 'w', encoding='utf-8') as f:
            f.write(h_content)
        print(f'Generated {h_path}')

        impl_c_content, impl_h_content = emit_c_service_impl(schema, rpc_h_basename)
        if impl_c_content:
            impl_c_path = os.path.join(out_dir, f'{base}.rpc.impl.c')
            impl_h_path = os.path.join(out_dir, f'{base}.rpc.impl.h')
            impl_user_path = os.path.join(out_dir, f'{base}.rpc.impl_user.c')
            with open(impl_c_path, 'w', encoding='utf-8') as f:
                f.write(impl_c_content)
            print(f'Generated {impl_c_path}')
            with open(impl_h_path, 'w', encoding='utf-8') as f:
                f.write(impl_h_content)
            print(f'Generated {impl_h_path}')
            # impl_user.c 仅在不存在时生成，避免覆盖用户实现
            impl_user_content = emit_c_service_impl_user(schema, rpc_h_basename)
            if impl_user_content and not os.path.exists(impl_user_path):
                with open(impl_user_path, 'w', encoding='utf-8') as f:
                    f.write(impl_user_content)
                print(f'Created {impl_user_path} (template, will not overwrite)')


if __name__ == '__main__':
    main()
