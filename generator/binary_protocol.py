"""
自定义二进制协议规范

帧格式: [1B method_id][2B invoke_id LE][2B payload_len LE][binary payload]
- invoke_id: 调用 ID，用于并发请求时匹配请求与响应。0 表示流式推送/请求

Payload 编码规则（请求与响应一致）:
- int/int32: 4B LE
- int64: 8B LE
- uint32/uint64: 4B/8B LE
- bool: 1B (0=false, 1=true)
- float: 4B IEEE754
- double: 8B IEEE754
- string: [2B len LE][utf8 bytes]
- enum: 4B LE (按 int 处理)
- OPTIONAL(T): [1B present][T if present]  (present=0 省略, present=1 紧跟值)
- struct: 按字段顺序依次编码
- LIST(T): [4B count LE][elem0][elem1]...
- MAP: 暂不支持（方法参数中少见）
"""
