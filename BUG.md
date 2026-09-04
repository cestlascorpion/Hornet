# Historical Bug Analysis

本文记录 Hornet 早期 Jaeger 二进制上下文实现中的问题、形成原因、测试为何没有发现，以及当前状态。Hornet 是实验性代码，以下内容用于保留实验结论，不应视为生产兼容性承诺。

## 二进制协议布局

`trace-ctx` 的二进制载荷按以下顺序编码：

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 16 | Trace Id |
| 16 | 8 | Span Id |
| 24 | 8 | Parent Span Id |
| 32 | 1 | Flags |
| 33 | 4 | Baggage entry count in big-endian order |
| 37 | Variable | Repeated key length, key, value length, value |

Id 字段是字节数组。它们的线上顺序就是其十六进制文本表示的顺序，不应按主机整数端序处理。

## Id 字节序错误

### 历史实现

旧 `Propagator.cpp` 先将 Id 字节强制转换为 `uint64_t`，再调用
`htobe64` 或 `be64toh`，并写回原始缓冲区：

```cpp
auto high = endian::fromBigEndian(*(uint64_t *)context.data());
*(uint64_t *)context.data() = high;
```

该实现同时存在两个问题：

- Id 不是数值字段，而是固定长度字节数组，不需要端序转换
- `uint64_t *` 可能未对齐，且对 carrier 返回的只读视图写入属于未定义行为

在小端机器上，输入的前 8 个 wire bytes
`FE A8 03 76 EE 0C 6F 9F` 会被按 `uint64_t` 读取为
`0x9F6F0CEE7603A8FE`。旧代码再进行字节交换并以小端形式写回，最终会把内存中的 8 个字节反转。

因此旧测试常量中的注释记录的是错误解析后的主机端结果，而不是 wire format 中的实际 Id：

| Field | Wire format | 旧注释中的错误值 |
| --- | --- | --- |
| Trace Id | `fea80376ee0c6f9fec9c673f09f6eae1` | `9f6f0cee7603a8fee1eaf6093f679cec` |
| Span Id | `9e5304ae5f1682bb` | `bb82165fae04539e` |

### 修复

`src/Binary.cpp` 现在只使用 `memcpy` 处理 Id 字节，长度和 baggage 数量使用显式的大端读写函数。`Propagator.cpp` 仅负责在 OpenTelemetry 对象和纯二进制数据结构之间转换。

## Flags 使用 ASCII 字符

### 历史实现

旧注入逻辑写入字符 `'1'` 或 `'0'`：

```cpp
buffer[flagsOffset] = sampled ? '1' : '0';
```

这会产生 `0x31` 或 `0x30`，而二进制协议需要 `0x01` 或 `0x00`。`0x31` 的最低位恰好为 1，因此某些本地 `IsSampled()` 检查仍返回真，掩盖了线上载荷不兼容的问题。

### 修复

纯编码器直接写入二进制 `0` 或 `1`。测试对完整样本进行字节级比较。

## 二进制测试误用 strcmp

### 历史实现

旧测试通过以下方式比较两个二进制字符串：

```cpp
strcmp(buffer, jtx.c_str())
```

样本的 parent span Id 从 offset 24 开始为全零，`strcmp` 会在第一个 `\0` 停止。它不会比较 flags、baggage 数量、key 或 value。也就是说，即使 flags 从 `0x01` 错误变为 `0x31`，测试仍可能通过。

### 修复

`test/Trace.cpp` 改为先比较长度，再使用 `memcmp`。不依赖 OpenTelemetry 的 `test/Binary.cpp` 还会检查样本 Id 的首尾字节，避免编码与解码同时出错时产生伪 round-trip。

## Parent Span Id 丢失

### 历史实现

旧 `Context::Context(const string &)` 中的 parent span Id 解析被注释掉。`FormatAsJaegerContext` 虽然把 `_parentSpanId` 转换到了临时缓冲区，但随后创建的 OpenTelemetry `SpanContext` 不包含 parent span Id，最终注入逻辑又将该字段置零。

因此非零 parent span Id 无法通过 `ParseFromJaegerContext` 和 `FormatAsJaegerContext` 保留。

### 修复

`Binary::Context` 显式保存 parent span Id。`FormatAsJaegerContext` 直接调用纯编码器，不再经过缺少 parent Id 的 OpenTelemetry `SpanContext`。独立测试覆盖非零 parent span Id 的完整 round-trip。

## 无效 Context 的默认 Id 长度错误

### 历史实现

旧默认值使用了 18 个零的 trace Id 和 9 个零的 span Id。正确长度分别为 32 和 16 个十六进制字符。旧十六进制转换函数允许变长输入并左侧补零，因此全零 parent span Id 样本意外没有失败。

### 修复

默认 trace Id 使用 32 个零，span Id 和 parent span Id 使用 16 个零。纯十六进制解码器要求长度精确匹配目标字段长度，并拒绝非法字符。

## RuntimeContext 生命周期错误

### 历史实现

`StartSpan` 调用了 `RuntimeContext::Attach`，但 `EndSpan` 只结束 span，没有调用 `Detach`。嵌套 span 结束后，已结束的子 span 仍可能是当前上下文，后续传播会使用错误的 span Id。

`StartIsolatedSpan` 也调用了 `Attach`，这与其“不设置 active”的接口语义相反。局部 token 被销毁并不会替代显式 `Detach`。

### 修复

`EndSpan` 结束 span 后显式分离 token。`StartIsolatedSpan` 使用一个临时 Context 注入传播数据，不再改变 RuntimeContext。`test/Trace.cpp` 增加嵌套 span 恢复和 isolated span 不污染当前上下文的检查。

## 构建目标依赖错误

### 历史实现

`Trace` 通过硬编码路径链接 `${PROJECT_BINARY_DIR}/libHornet.a`。CMake 无法从这个路径推导出对 `Hornet` 目标的依赖，并行构建可能在静态库生成前尝试链接测试程序。

### 修复

`Trace` 现在直接链接 `Hornet` 目标。二进制编解码器拆为 `HornetBinary`，其测试可在没有 OpenTelemetry 头文件或库的环境中单独构建和运行。

## 尚未处理的采样热加载并发问题

`src/Sampler.cpp` 的白名单热加载仍可能与 `CheckPass` 并发执行。旧双缓冲设计中的 `_idx` 从未切换，加载线程会修改正在被读取的 `std::set`，存在数据竞争。`lastLoadTs` 在配置文件未修改时也不会更新，超过首次检查间隔后会在每次采样时执行 `stat`。

该问题不属于本次二进制上下文修复范围。若实验需要多线程采样并支持运行时改配置，应使用互斥锁，或以不可变快照和原子共享指针替换当前 `std::set`。
