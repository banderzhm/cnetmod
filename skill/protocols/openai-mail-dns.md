# OpenAI / Mail / DNS

> OpenAI API 异步客户端（Chat/Embedding/TTS/STT/DALL-E）、SMTP 邮件收发、异步 DNS 客户端与服务端。

**import**:
- `import cnetmod.protocol.openai;`
- `import cnetmod.protocol.mail;`
- `import cnetmod.protocol.dns;`

**CMake**:
- `-DCNETMOD_ENABLE_OPENAI=ON`
- `-DCNETMOD_ENABLE_MAIL=ON`
- `-DCNETMOD_ENABLE_DNS=ON`

**源码**:
- `src/protocol/openai/`
- `src/protocol/mail/`
- `src/protocol/dns/`

---

## Part 1: OpenAI

### 场景导航

- 我要调用 Chat Completions → [看这里](#场景chat-completions)
- 我要使用 Responses API → [看这里](#场景responses-api)
- 我要流式接收响应（SSE） → [看这里](#场景流式-chat-sse)
- 我要构建可组合链/结构化输出 → [看这里](#场景runnable-与结构化输出)
- 我要构建工具调用 Agent → [看这里](#场景工具调用-agent)
- 我要做对话记忆与 RAG → [看这里](#场景对话记忆与-rag)
- 我要增加重试、模型回退、取消和追踪 → [看这里](#场景韧性取消与运行追踪)
- 我要生成 Embedding 向量 → [看这里](#场景embeddings)
- 我要生成图片（DALL-E） → [看这里](#场景dall-e-图片生成)
- 我要语音合成/识别 → [看这里](#场景tts--stt)

### API 参考

#### `connect_options` — 连接配置

**签名**: `export struct connect_options`

```cpp
struct connect_options {
    std::string api_base = "https://api.openai.com/v1";
    std::string api_key;
    bool tls_verify = true;
    std::string tls_ca_file;
    int timeout_seconds = 120;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};
```

#### `message` — 对话协议消息

**签名**: `export struct message`

| 方法 | 签名 | 说明 |
|------|------|------|
| `user` | `static auto user(std::string_view text) -> message` | 构造终端用户输入（协议角色 `user`） |
| `system` | `static auto system(std::string_view text) -> message` | 构造系统级指令（协议角色 `system`） |
| `developer` | `static auto developer(std::string_view text) -> message` | 构造应用开发者指令（协议角色 `developer`） |
| `model_output` | `static auto model_output(std::string_view text) -> message` | 构造模型输出消息（序列化协议角色为 `assistant`） |
| `tool_call_request` | `static auto tool_call_request(std::vector<tool_call>) -> message` | 构造模型发起的工具调用请求，并携带调用标识与参数 |
| `tool_result` | `static auto tool_result(std::string_view id, std::string_view content) -> message` | 构造与工具调用标识关联的执行结果（协议角色 `tool`） |
| `user_multimodal` | `static auto user_multimodal(std::vector<content_part>) -> message` | 构造包含文本、图像等内容分片的用户输入 |

#### `chat_request` / `chat_response` — 请求与响应

**签名**: `export struct chat_request`

```cpp
struct chat_request {
    std::string model = "gpt-4o-mini";
    std::vector<message> messages;
    double temperature = 0.7;
    int max_tokens = 4096;
    bool stream = false;
    std::vector<tool> tools;
    std::string tool_choice; // "auto" | "none" | "required"
    std::string response_format; // "" | "json_object" | "json_schema"
    std::optional<int> seed;
};
```

**签名**: `export struct chat_response`

```cpp
struct chat_response {
    std::string id;
    std::string model;
    std::vector<choice> choices;
    usage token_usage;
    auto content() const -> std::string_view; // choices[0].msg.content
};
```

#### `client` — OpenAI 客户端

**签名**: `export class client`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit client(io_context&) noexcept` | |
| `connect` | `auto connect(connect_options) -> task<std::expected<void, std::string>>` | 连接 API |
| `chat` | `auto chat(chat_request) -> task<std::expected<chat_response, std::string>>` | Chat Completions |
| `responses` | `auto responses(response_request) -> task<std::expected<response_result, std::string>>` | Responses API |
| `chat_stream` | `auto chat_stream(chat_request, on_chunk_fn[, cancel_token&]) -> task<std::expected<std::string, std::string>>` | SSE 流式，可取消 |
| `chat_stream_async` | `auto chat_stream_async(chat_request, async_chunk_fn[, cancel_token&]) -> task<...>` | 异步回调流式，可取消 |
| `list_models` | `auto list_models() -> task<std::expected<std::vector<model_info>, std::string>>` | 列出模型 |
| `embeddings` | `auto embeddings(embedding_request) -> task<std::expected<embedding_response, std::string>>` | 向量嵌入 |
| `text_to_speech` | `auto text_to_speech(tts_request) -> task<std::expected<std::vector<std::byte>, std::string>>` | 语音合成 |
| `transcribe` | `auto transcribe(transcription_request) -> task<std::expected<transcription_response, std::string>>` | 语音转文字 |
| `translate` | `auto translate(translation_request) -> task<std::expected<transcription_response, std::string>>` | 语音翻译 |
| `create_image` | `auto create_image(image_generation_request) -> task<std::expected<image_response, std::string>>` | 生成图片 |
| `edit_image` | `auto edit_image(image_edit_request) -> task<std::expected<image_response, std::string>>` | 编辑图片 |
| `create_image_variation` | `auto create_image_variation(image_variation_request) -> task<...>` | 图片变体 |
| `moderate` | `auto moderate(moderation_request) -> task<std::expected<moderation_response, std::string>>` | 内容审核 |

#### 多模态与 Function Calling 类型

```cpp
export struct content_part {
    std::string type;           // "text" | "image_url"
    std::string text;
    image_url_detail image_url;
    static auto make_text(std::string_view) -> content_part;
    static auto make_image_url(std::string_view url, std::string_view detail = "auto") -> content_part;
    static auto make_image_base64(std::string_view data, std::string_view media_type, std::string_view detail) -> content_part;
};

export struct tool_call { std::string id; std::string type; function_call function; };
export struct tool { std::string type; std::string function_name; std::string function_description; json function_parameters; };
```

#### 模型、链、Agent 与检索抽象

| 类型 | 设计职责 |
|------|----------|
| `chat_model` / `embedding_model` | Strategy：隔离供应商客户端，方便 fake、替换和组合 |
| `openai_chat_model` / `openai_embedding_model` | Adapter：将底层 `client` 接入模型抽象 |
| `chat_model_router` / `routed_chat_model` | Strategy：按每次调用上下文异步选择模型，并支持无匹配时回退 |
| `image_model` / `moderation_model` | Strategy：供应商无关的图像生成与内容审核模型契约 |
| `openai_image_model` / `openai_moderation_model` | Adapter：将底层图像和审核端点接入统一模型抽象 |
| `telemetry_listener` | Observer：输出 OpenMetrics 指标、成本估算和 W3C 关联 Span，可直接接入 OTLP/HTTP |
| `run_scope` | RAII：保证模型、工具、检索和 Agent 在成功、错误、取消与异常路径都闭合观测生命周期 |
| `resilient_chat_model` | Decorator：普通及流式调用的指数退避重试与有序模型回退；流已开始后禁止重放 |
| `governed_chat_model` | Decorator：对普通及流式调用应用 Bulkhead 并发隔离、Token Bucket 速率限制与 Circuit Breaker 熔断保护 |
| `runnable` | Composite/Pipeline：按顺序组合 prompt、model、parser 等异步步骤 |
| `prompt_template` / `chat_prompt_template` | 严格变量、默认值、条件段与列表 section；`{{`/`}}` 表示字面花括号 |
| `json_output_parser` | 解析 JSON 并校验 `type/required/properties/items/enum/additionalProperties` 子集 |
| `tool_registry` | Command Registry：注册异步工具，执行前校验 JSON Schema，并以结构化错误区分取消、未注册、参数无效和执行失败 |
| `tool_provider` / `functional_tool_provider` | Strategy：根据会话、调用参数和工具循环迭代动态提供工具 |
| `keyword_tool_search` / `semantic_tool_search` | Strategy：按关键词或向量相似度发现工具，避免完整工具目录占用上下文 |
| `tool_binding<Arguments, Result>` | Adapter：将强类型 C++ 异步命令绑定到 JSON Tool Calling 协议 |
| `agent_executor` | State：有界执行 model → tools → model 循环 |
| `conversation_memory` | Repository：协程安全、有界的会话消息存储 |
| `append_only_chat_memory_store` | Repository：面向消息表的原子追加与最近窗口读取，不重写历史快照 |
| `append_only_chat_record_store` | Repository：协议消息与应用元数据分离，追加后返回数据库补全的记录 |
| `chat_record_memory_adapter` | Adapter：将带 ID、模型、token、时间戳等元数据的记录仓库接入协议记忆 |
| `file_chat_memory_store` | Repository：按会话分文件、容量受限并以原子替换持久化完整消息 |
| `long_term_store` | Repository：跨会话 namespace/key JSON 记忆、TTL、过滤分页与可选语义检索 |
| `checkpoint_store` | Repository：版本化执行状态、pending writes、分支、回滚与乐观并发 |
| `checkpoint_agentic_scope_store` | Adapter：将通用 Checkpointer 接入 `agentic_runtime` |
| `retriever` / `embedding_store` | Repository/Strategy：供应商无关的过滤检索与向量存储契约 |
| `in_memory_vector_store` | Repository：异步嵌入、余弦检索、更新和删除 |
| `functional_retriever` | Adapter：将全文搜索、知识图谱、SQL、Web 搜索或应用检索函数接入统一检索契约 |
| `delegating_embedding_store` | Adapter：按能力组合外部向量仓库的写入、检索、删除、清空和计数处理器 |
| `metadata_filter` | Composite：嵌套字段条件、AND、OR 与 NOT 元数据表达式 |
| `retrieval_chain` | RAG Pipeline：retrieve → prompt context → model |
| `ai_service` | Facade：统一编排模型、会话记忆、工具、RAG 与输入/输出策略 |
| `structured_service<T>` | Typed Facade：JSON Schema 约束、本地校验并解码为 C++ 业务对象 |
| `guardrail_pipeline` | Chain of Responsibility：组合输入校验、内容审核、输出校验与重试 |
| `retrieval_augmentor` | Advanced RAG：查询转换、路由、多路检索、RRF、重排与上下文注入 |
| `citation_context_injector` | Strategy：以稳定来源标签、文档 ID、分数和白名单元数据注入可追溯上下文 |
| `model_query_router` | Strategy：使用严格结构化模型输出选择具名检索器，并提供 fail/none/all 回退策略 |
| `model_query_transformer` | Strategy：严格结构化地执行查询压缩、改写、多查询扩展和 HyDE |
| `functional_query_transformer` / `functional_query_router` | Adapter：接入应用自定义异步查询转换与路由策略 |
| `functional_content_aggregator` / `functional_content_reranker` / `functional_context_injector` | Adapter：替换聚合、重排和上下文注入阶段 |
| `scoring_model` / `scoring_reranker` | Strategy：供应商无关的相关性评分与确定性过滤、重排 |
| `chat_scoring_model` | Adapter：使用严格结构化聊天输出为候选文档逐项评分 |
| `ingestion_pipeline` | Pipeline：文档加载、可组合转换、分块与索引写入 |
| `metadata_enricher` / `document_filter` / `functional_document_transformer` | Strategy/Adapter：元数据增强、文档筛选与应用自定义异步转换 |
| `recursive_text_splitter` / `markdown_header_splitter` | Strategy：递归边界文本分块或保留标题层级元数据的 Markdown 分段 |
| `file_document_source` / `directory_document_source` / `url_document_source` | Source/Composite：异步加载指定文件、目录树或远程 URL |
| `document_parser` / `document_parser_registry` | Strategy/Registry：按扩展名或媒体类型选择可替换解析器，把来源读取与格式解析分离 |
| `functional_document_parser` | Adapter：接入异步 PDF、Office、Tika、Docling 等外部解析实现 |
| `markdown_document_parser` / `html_document_parser` | Strategy：内置 Markdown 元数据解析与 HTML 可见正文提取 |
| `evaluation_suite` | Composite：组合确定性匹配和嵌入语义相似度评测，输出逐项分数及汇总报告 |
| `agentic_runtime` / `workflow_planner` | Template Method + Strategy：并行 Agent、共享 Scope、人工审批与检查点恢复 |
| `file_agentic_scope_store` | Repository：按工作流隔离、容量受限并以原子替换持久化 Scope、Planner 和人工审批状态 |
| `parallel_planner` / `conditional_planner` / `loop_planner` | Strategy：开箱即用的并行、条件分支与有界循环编排 |
| `mcp_client` | Facade/Adapter：MCP 初始化、工具、资源、提示词与本地 Tool Registry 适配 |
| `mcp_tool_provider` | Adapter/Strategy：聚合多个 MCP 客户端，按客户端及工具定义过滤并动态暴露工具 |
| `skill_catalog` / `filesystem_skill_loader` | Repository/Provider：预加载 Agent Skills，并在激活后渐进披露资源与专属工具 |
| `mcp_stdio_transport` | Strategy：跨平台子进程 stdio JSON-RPC，并通过 executor bridge 避免阻塞 I/O 协程 |

协议契约按业务领域直接分区：

| 分区 | 职责 |
|------|------|
| `:foundation` | JSON 别名、usage、错误、连接与模型信息 |
| `:tool_contracts` | 函数工具声明与工具调用结果 |
| `:messages` | 文本、多模态与工具消息 |
| `:chat` | Chat Completions 请求、响应与流式 chunk |
| `:responses` | Responses API 请求与结果 |
| `:embeddings` | Embedding 请求与向量响应 |
| `:audio` | TTS、转录与翻译 |
| `:images` | 图片生成、编辑和变体 |
| `:moderation` | 内容审核 |
| `:guardrails` | 输入/输出策略链与审核适配器 |
| `:service` | 高层 AI Service 编排门面 |
| `:structured` | 强类型结构化输出契约与业务对象解码 |
| `:filters` | 可组合元数据过滤表达式 |
| `:rag` | Advanced RAG 扩展点与默认实现 |
| `:ingestion` | 文档摄取流水线 |
| `:loaders` | 基于异步文件 I/O 的具体文档来源 |
| `:evaluation` | 可组合的响应质量评测与汇总报告 |
| `:bindings` | 强类型工具参数解码、执行与结果编码适配 |
| `:tool_search` | 关键词与向量语义工具检索策略 |
| `:skills` | Agent Skills 目录加载、激活、资源读取与技能专属工具供应 |
| `:memory_store` | 基于文件系统的持久化 Chat Memory Store |
| `:long_term_store` | 跨会话、具名空间、可检索的长期 JSON Memory Store |
| `:checkpoint` | 版本化 Checkpointer、pending writes、分支和回滚契约 |
| `:agentic` | 多 Agent 工作流运行时与持久化 Scope |
| `:planners` | 可恢复的并行、条件与循环 Planner |
| `:mcp` | MCP client、Streamable HTTP/stdio transport 与 Tool Adapter |

所有高层调用均接收 `run_config`。可通过 `run_id`、`tags`、`metadata` 传播运行上下文，通过 `callback` 接收模型、重试、工具、检索和 Agent 生命周期事件，通过 `cancel_token` 协作式取消。上下文感知工具、动态工具提供器和检索器会收到同一次调用配置的非拥有视图，不得在对应异步操作完成后保留该指针或引用。

`run_config.listeners` 可同时安装多个 `run_listener`，以 Observer 方式接收嵌套调用事件；`callback` 作为轻量兼容入口继续保留。每个 `run_event` 带时间戳和结构化 `attributes`，便于映射 OpenTelemetry GenAI 语义字段或指标标签。监听器和兼容回调相互隔离：单个观察者抛出的异常会记录警告但不会中断后续观察者或业务调用；`functional_run_listener` 可将应用函数直接适配为观察者。

`conversation_memory` 同时支持消息数量窗口与 token 窗口。使用 `chat_memory_store` 可以按 session ID 持久化完整快照；使用 `append_only_chat_memory_store` 时，追加直接进入消息表，读取只请求最近窗口，裁剪不会回写数据库。需要保留数据库生成的消息 ID、模型、token 和时间戳时，实现 `append_only_chat_record_store`：`persisted_chat_message` 将协议 `message` 与任意 JSON metadata 分离，追加返回数据库补全后的记录；`load_page(session_id, offset, limit)` 按插入顺序分页，`count(session_id)` 单独返回总数，零 `limit` 返回空页；`chat_record_memory_adapter` 只向模型暴露协议消息，metadata 永远不会进入 OpenAI 请求。记录仓库统一返回 `std::error_code`，可用 `chat_record_store_errc` 区分会话不存在、参数错误、冲突、存储不可用、数据损坏、资源耗尽和原子写失败。`append_batch` 是明确的原子存储边界，实现必须使用数据库原生事务或等效的原子操作，框架不会泄漏一套无法覆盖 SQL 与非 SQL 存储的伪事务对象。`trim_messages` 是公开的无持久化纯算法，下游也可以直接复用窗口与工具交换裁剪策略。`pinned_prefix_messages` 保护固定前缀并将其排除在消息数和 token 预算之外，`preserved_tail_messages` 默认保护最后一条消息。裁剪返回 `trim_result`，报告删除消息数、删除 token、剩余 token 及预算是否真正满足；`remaining_tokens` 只统计受预算约束的后缀，不包含 pinned 前缀，并直接与非零 `max_tokens` 比较。只剩受保护消息时不会为了硬凑预算删除本轮输入。`max_messages` 与 `max_tokens` 的零值均表示不限制。存储失败会沿协程调用链返回；淘汰模型工具调用时会同时清理关联的工具结果，避免产生孤立协议消息。

`prompt_template` 保留 `{name}` 缺失即报错的严格行为，并增加 `{name|default}` 默认值、`{?name}...{/name}` 条件段及 `{#items}...{/items}` 列表 section。列表的每一行使用 `prompt_section`，拥有局部变量和可递归的子 section；变量及子 section 都按“当前行优先、根上下文兜底”解析。富上下文通过 `prompt_context` 和 `format_context()` 显式传入，避免与旧的花括号 `prompt_variables` 调用产生重载歧义。`output_parser` 仍是按需组合的结构化输出边界，普通文本业务不需要为了使用 Prompt 或 Memory 强制接入 Parser。

`file_chat_memory_store` 提供开箱即用的持久化实现。每个 session 使用独立 JSON 文件，session ID 先稳定哈希为安全文件名并保存在文件信封中二次校验；写入使用同目录临时文件和原子替换。文件访问通过 executor bridge 执行，支持文本、多模态、模型工具调用和工具执行结果的完整往返恢复，并限制单会话文件大小。

`long_term_store` 面向跨会话记忆，不与聊天记录混用。`store_namespace` 提供层级隔离，`put` 保存任意 JSON 并返回单键递增版本，`expected_version` 以 compare-and-swap 防止覆盖并发更新；`search` 支持 namespace 前缀、`metadata_filter`、limit/offset 分页、TTL 刷新和可选 embedding 相似度。`in_memory_long_term_store` 是具备完整语义的参考实现；生产数据库通过相同接口在事务中实现版本、过期和索引。

`checkpoint_store` 保存不可变状态版本，并为每一版本维护幂等的 pending-write journal。`commit`、`put_pending_writes` 和 `rollback` 分别提供 head version 或 write revision 的乐观并发检查；`fork` 从指定历史版本创建隔离分支，`rollback` 通过追加新版本恢复旧状态，不删除审计历史。`checkpoint_agentic_scope_store` 将该契约适配到现有 `agentic_runtime`，因此 Agent 的暂停、恢复和每步保存自动得到版本历史与并发冲突保护。

`ai_service` 是推荐的应用层入口。它在一次调用内按顺序执行输入 Guardrail、会话读取、Advanced RAG、模型或工具 Agent、输出 Guardrail 以及会话提交；不合规输出可在限定次数内带修正指令重新生成。工具 Agent 返回完整 `transcript`，AI Service 会把模型工具请求、带调用标识的工具结果和最终模型输出作为一个连续交换提交到记忆，不会只保留最终文本而破坏下一轮协议上下文。

`retrieval_augmentor` 将 `query_transformer`、`query_router`、`content_aggregator`、`content_reranker` 与 `context_injector` 作为独立策略组合。默认提供静态路由、Reciprocal Rank Fusion、透传重排器、developer context 注入器及引用溯源注入器。`citation_context_injector` 为每个候选生成稳定的 `[source N]` 标签，可选择附带文档 ID、相关性分数和显式白名单中的元数据，避免把私有元数据无意发送给模型。

`functional_retriever` 可把全文搜索引擎、知识图谱、SQL、Web 搜索或应用函数直接接入 RAG。`delegating_embedding_store` 将外部向量数据库的能力拆成独立异步处理器，检索为必需能力，写入、按 ID 删除、按元数据删除、清空和计数可以按后端实际能力选择性提供；未实现的操作返回明确的“不支持”错误，而不是静默丢弃。这样 PostgreSQL、Redis、Milvus、Pinecone 等集成可复用完整的过滤、摄取、路由、重排和 AI Service 流水线。

`model_query_router` 为每个检索器绑定稳定名称、用途描述和实例指针，使用温度 0 及严格 JSON Schema 让模型选择相关检索器。返回结果会再次本地校验并映射到已注册实例；模型错误、非法 JSON 或未知名称按配置选择直接失败、不路由或路由到全部检索器，避免模型输出直接控制未注册资源。

`model_query_transformer` 使用温度 0 和严格 JSON Schema 实现四种检索前变换：将带上下文问题压缩成独立查询、精确改写、生成去重的多查询扩展，以及生成仅用于向量检索的假设文档（HyDE）。它限制最多 32 个结果，拒绝空白、超量和无效结构，保留原查询的过滤条件、阈值、数量限制及元数据，并可显式保留原始查询。所有 Advanced RAG 阶段都有对应的 `functional_*` Adapter，应用可以替换任一策略而不继承框架实现。

为 `retrieval_augmentor` 提供 `io_context` 时，多查询与多检索器形成的检索任务通过 `task_group` 并发执行，结果仍按确定性任务顺序交给聚合器；每个子任务拥有独立取消令牌，并通过 `retrieval_request::config` 向异步存储或搜索引擎传播。任一任务失败会取消同组任务并在全部子任务退出后返回，且优先保留原始检索错误而不是后续取消错误，避免孤立协程、静默后台工作和诊断信息丢失；不提供执行上下文时保留顺序执行模式。

`scoring_model` 将交叉编码器、专用 rerank API 或应用自定义算法统一为批量相关性评分契约，`functional_scoring_model` 可直接适配异步业务实现。`scoring_reranker` 校验评分数量和有限值，按阈值过滤后稳定降序排列；`chat_scoring_model` 可在没有专用 rerank 服务时使用聊天模型，限制每篇候选内容长度，通过严格 JSON Schema 和本地索引完整性检查保证每篇文档恰好得到一个 `[0, 1]` 分数。

`agentic_runtime` 使用 `workflow_planner` 决定下一组 Agent，同组 Agent 通过 `task_group` 并行执行。每个成功步骤都会把共享 `agentic_scope` 和 Planner 状态写入 `agentic_scope_store`；`human_input_request` 可携带请求标识、提示、写回键与 JSON Schema，`resume` 验证人工回复后从检查点继续，重复 `execute` 不会绕过待处理审批。除轻量的内存仓库外，`file_agentic_scope_store` 在 executor 上执行文件访问，以工作流 ID 的稳定哈希隔离文件，通过信封二次校验原始 ID，限制检查点容量，并用同目录临时文件原子替换目标文件；进程重启后仍可恢复 Planner、共享 Scope、已完成步骤和待处理人工审批。

`structured_service<T>` 接收显式 `structured_output_contract<T>`，向模型发送严格 JSON Schema，并在边界再次校验 JSON 后调用业务解码器。该设计不依赖反射宏，解码失败通过 `std::expected` 返回，同时保留 `ai_service_result` 中的原始消息、token 用量、检索文档和工具步骤。

`governed_chat_model` 复用 cnetmod 的协程信号量、通用 Token Bucket 和断路器，实现并发隔离、请求速率限制与快速失败；它可与负责重试/模型故障转移的 `resilient_chat_model` 按装饰器顺序组合。两者均保留真正的异步流式调用，不会退化为完整响应后再伪造单个 chunk；重试仅允许发生在尚未向消费者交付任何 chunk 时，防止部分输出被重复或与备用供应商输出拼接。

`routed_chat_model` 在每次普通或流式调用前通过 `chat_model_router` 异步选择模型。`functional_chat_model_router` 可根据 `run_config` 中的租户、任务等级、成本或区域元数据实现应用策略；路由失败或未选择模型时可使用显式 fallback。路由层仍实现统一 `chat_model` 契约，因此可继续与重试、治理、Agent 和 AI Service 组合。

`telemetry_listener` 将 `run_config` 的嵌套生命周期事件转成低基数 OpenMetrics：操作总数、活动数、延迟直方图、输入/输出 token、重试、治理拒绝、Span 丢弃和按配置价格估算的成本。相同 `run_id` 内的 Agent、模型、工具与检索 Span 共享 W3C Trace ID；可使用通用 `span_exporter`，也可直接连接有界非阻塞的 `otlp_http_exporter`。提示词、输出和工具参数默认不进入 Span，必须显式启用且受单属性容量限制；运行 ID、租户和标签不会进入指标标签，避免高基数污染。`run_scope` 以 RAII 保证正常返回、业务错误、取消和异常退出都产生配对结束事件，并用唯一操作标识正确关联并行调用。

`image_model` 与 `moderation_model` 将图像生成和内容审核提升为可替换的模型策略，统一接收 `run_config` 并在发起网络请求前检查取消状态。内置 OpenAI Adapter 复用现有异步客户端并发出一致的模型生命周期事件，应用也可以实现相同契约接入其他供应商。`moderation_input_guardrail` 优先依赖这一供应商无关契约，同时保留直接接收 `client` 的兼容构造方式。

`chat_model::stream` 是供应商无关的异步流式接口，消费者返回 `task<bool>` 形成自然背压并可提前终止。OpenAI 适配器会聚合文本、工具调用分片、finish reason 与 token 用量；`agent_executor::stream` 和 `ai_service::stream` 将流式能力贯通工具循环、RAG、记忆和 Guardrail。输出策略触发重新生成时，`chat_chunk::generation_attempt` 用于区分每次候选输出。

`file_document_source`（兼容名 `text_file_source`）使用 `async_file_read_all` 加载文件，并保留来源路径、文件名和扩展名元数据；不会在协程中执行同步文件读取。`directory_document_source` 将目录发现工作桥接到 `thread_pool`，支持递归开关、大小写无关的扩展名白名单、文件数量和单文件容量上限、不可读文件策略；它忽略符号链接，以标准化路径排序后再异步读取，并附加 `source_root` 元数据，因此相同目录的摄取顺序可重复。`url_document_source` 通过可替换 `document_fetcher` 获取远程内容；内置 `http_document_fetcher` 复用 cnetmod 异步 HTTP 客户端，传播取消、验证 2xx 状态并限制响应体容量，同时保存 URL 与 Content-Type 元数据。

`document_parser_registry` 根据标准化扩展名或 HTTP `Content-Type` 选择 `document_parser`，媒体类型优先且会忽略大小写和参数，也可配置兜底解析器；文件与目录 Source 只负责取得字节和来源元数据。内置 `plain_text_document_parser` 处理 UTF-8 BOM、非法 UTF-8、嵌入 NUL 和空文档；`markdown_document_parser` 提取有界的 YAML 风格 front matter 与一级标题，并可在索引内容中移除 front matter；`html_document_parser` 提取标题、解码常用及数字实体、保留块级换行，并排除 `head`、脚本、样式、模板和 `noscript` 内容。`functional_document_parser` 可将 PDF、Office、Tika、Docling 等异步业务解析器适配到同一契约，无需修改摄取流水线。

摄取流水线可依次组合任意多个 `document_transformer`。内置 `metadata_enricher` 以可配置覆盖策略补充来源标签，`document_filter` 按应用谓词筛选文档，`functional_document_transformer` 将业务异步转换适配到统一接口；所有转换都传播 `run_config` 取消状态。`recursive_text_splitter` 适合一般文本并保留字符偏移，`markdown_header_splitter` 按 ATX 标题分段并附加标题、层级、源文档和稳定 section ID，便于检索结果回溯原章节。

`evaluation_suite` 对一组 `evaluation_case` 运行多个 `response_evaluator`，保留每个用例、每个评测器的分数、通过状态与解释，并计算总体通过/失败数量和平均分。内置 `exact_match_evaluator` 支持大小写及空白归一化，`semantic_similarity_evaluator` 复用供应商无关的 `embedding_model` 计算余弦相似度和通过阈值；`model_judge_evaluator` 使用确定性温度和严格 JSON Schema 约束模型裁判的分数与解释，并由应用阈值决定是否通过。

`bind_tool` 使用 `tool_binding<Arguments, Result>` 将协议边界的 JSON Schema 校验、参数解码、强类型异步命令和结果编码分离。业务处理函数直接接收 C++ 参数对象，不必在命令实现中手工读取 JSON 字段。

`tool_provider` 在 Agent 每次调用开始时解析可见工具；动态供应器还能在每轮模型调用前根据完整会话重新解析。`tool_provider_request` 携带会话消息、session ID、调用参数、迭代次数和调用配置。每个 `executable_tool` 可用 `tool_return_behavior` 选择把结果返回模型、立即作为最终结果返回，或仅在它是本轮最后一个调用时立即返回；需要贯穿取消、租户信息或追踪上下文时使用 `contextual_tool_handler`，普通一参数 `tool_handler` 保持兼容。

`tool_registry::invoke_detailed` 返回 `tool_error`，其 `tool_error_kind` 明确区分取消、工具不存在、参数解析或 Schema 校验失败以及业务处理失败。`agent_options::handle_tool_error` 可按应用策略把可恢复错误转换为模型可见结果，或终止本次调用；未配置时继续兼容 `continue_on_tool_error`。向 Agent 提供专用 `io_context` 并调用 `with_parallel_tool_execution` 后，同一轮的多个工具调用使用结构化 `task_group` 并发执行，等待全部子任务结束后仍按模型给出的调用顺序写回消息。上下文感知工具可在长操作内部检查取消令牌；并行工具处理函数及运行监听器必须自行满足并发安全要求。

启用 `agent_executor::with_tool_search` 后，默认工具不会一次性暴露给模型，只保留 `tool_visibility::always_visible` 工具和 `find_tools` 检索命令。模型完成检索后，命中的工具从下一轮开始可见。`keyword_tool_search` 提供无外部依赖的确定性匹配；`semantic_tool_search` 通过供应商无关的 `embedding_model` 按向量相似度排序。

`skill_catalog` 实现渐进式 Agent Skills：初始只暴露激活、停用和资源读取命令；`activate_skill` 的工具结果保存技能激活状态和指令，下一轮才加入技能专属工具及嵌套 Tool Provider。`filesystem_skill_loader` 在 executor 上预加载 `SKILL.md` 与相对资源，限制单文件和总容量、忽略符号链接及隐藏文件，模型推理期间不直接访问文件系统。

`mcp_client` 支持 MCP 初始化协商、ping、`tools/list`、`tools/call`、资源/资源模板、资源订阅与退订、提示词、参数补全和服务端日志级别 API，并可把远程工具注册到 `tool_registry`。`mcp_tool_provider` 可聚合多个客户端，使用任意多个 client-aware filter 限制工具，允许运行时增删客户端，并可选择忽略单个服务失败或整体失败；同名工具必须通过过滤器消歧。客户端入站分派器支持 `sampling/createMessage`、`roots/list`、`elicitation/create` 和通知回调，因此资源更新、日志等服务端通知可由应用统一处理。`mcp_streamable_http_transport` 可处理包含多个 JSON-RPC 事件的 JSON/SSE 响应、回复服务端请求并传播 session；`mcp_stdio_transport` 使用 `child_process` 和 executor bridge 与本地 MCP server 双向交换换行分隔 JSON-RPC，并确定性回收子进程。

`response_request` 对常用字段提供强类型成员，包括 `previous_response_id`、`tool_outputs`、`max_output_tokens`、`max_tool_calls`、`reasoning`、`service_tier`、`prompt_cache_key` 和 `safety_identifier`。托管工具/MCP 工具及新增输入项可分别通过 `additional_tools`、`additional_input_items` 扩展；尚未建模的新字段可放入 `extra_body`（最后合并，同名字段会覆盖强类型序列化结果）。

### 场景：Chat Completions

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.openai;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::openai::client client(ctx);
    co_await client.connect({.api_key = "sk-..."});

    cn::openai::chat_request req{
        .model = "gpt-4o-mini",
        .messages = {
            cn::openai::message::system("You are a helpful assistant."),
            cn::openai::message::user("What is C++23?"),
        },
        .temperature = 0.7,
        .max_tokens = 512,
    };

    auto resp = co_await client.chat(req);
    if (resp) {
        cn::logger::info{"Reply: {}", resp->content()};
        cn::logger::info{"Tokens: prompt={}, completion={}",
            resp->token_usage.prompt_tokens, resp->token_usage.completion_tokens};
    }
    ctx.stop();
}

auto main() -> int {
    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run(*ctx));
    ctx->run();
}
```

### 场景：Responses API

```cpp
cn::openai::response_request req{
    .model = "gpt-4.1-mini",
    .input = {cn::openai::message::user("Return a concise summary")},
    .instructions = "Be factual.",
    .max_output_tokens = 512,
    .prompt_cache_key = "summary-v1",
    .response_schema_name = "summary",
    .response_schema = {
        {"type", "object"},
        {"properties", {{"summary", {{"type", "string"}}}}},
        {"required", {"summary"}},
        {"additionalProperties", false},
    },
};
auto response = co_await client.responses(std::move(req));
if (response)
    cn::logger::info{"Response: {}", response->output_text};
```

### 场景：流式 Chat（SSE）

```cpp
cn::openai::chat_request req{
    .model = "gpt-4o-mini",
    .messages = {cn::openai::message::user("Explain async programming")},
    .stream = true,
};

// 同步回调版本
auto full = co_await client.chat_stream(req, [](const cn::openai::chat_chunk& chunk) {
    cn::logger::info{"{}", chunk.delta_content};
});

// 异步回调版本（可在回调中 co_await）
auto full = co_await client.chat_stream_async(req,
    [](const cn::openai::chat_chunk& chunk) -> cn::task<bool> {
        cn::logger::info{"{}", chunk.delta_content};
        co_return true; // return false to abort
    });

// 调用方取消会中止挂起的网络读取并淘汰当前连接。
cn::cancel_token cancellation;
auto cancellable = co_await client.chat_stream_async(req, callback, cancellation);
```

流式回调按完整 SSE event 实时触发，不等待整个 HTTP body。客户端兼容
`data:` 与 `data: `，并以 `[DONE]`、任意非空 `finish_reason`、HTTP
分帧结束或连接关闭作为完成边界。请求 `stream_options.include_usage` 时，
客户端会在 `finish_reason` 后继续接收独立 usage 尾帧，并使用一秒有界等待
兼容省略 usage 与 `[DONE]` 的网关。消费端返回 `false` 时立即关闭当前连接，
避免未消费的增量污染下一次请求。
每次流式网络读取受 `connect_options::timeout_seconds` 限制；调用方取消、
读取超时、写入失败和解析失败都会关闭连接，后续请求通过自动重连获得干净会话。

### 场景：Runnable 与结构化输出

```cpp
cn::openai::chat_prompt_template prompt{{
    {.role = "system", .prompt = cn::openai::prompt_template{"Return JSON only."}},
    {.role = "user", .prompt = cn::openai::prompt_template{"Question: {question}"}},
}};
auto parser = std::make_shared<cn::openai::json_output_parser>(
    cn::openai::json{{"type", "object"},
        {"properties", {{"answer", {{"type", "string"}}}}},
        {"required", {"answer"}}});

cn::openai::openai_chat_model model{client};
cn::openai::runnable chain{cn::openai::prompt_runnable(std::move(prompt))};
chain = chain.pipe(cn::openai::model_runnable(model))
            .pipe(cn::openai::parser_runnable(std::move(parser)));
auto result = co_await chain.invoke(
    cn::openai::prompt_variables{{"question", "What is C++23?"}});
```

普通文本输出不需要安装 `output_parser`。条件与列表 Prompt 使用显式
`prompt_context`：

```cpp
cn::openai::prompt_template scoped{
    "{?title}{title}\n{/title}"
    "{#scopes}- {name}: {value|unset}\n{/scopes}"};
cn::openai::prompt_context values{
    .variables = {{"title", "Permissions"}},
    .sections = {{"scopes", {
        cn::openai::prompt_section{{{"name", "read"}, {"value", "allowed"}}},
        cn::openai::prompt_section{{{"name", "write"}}},
    }}},
};
auto rendered = scoped.format_context(values);
```

### 场景：工具调用 Agent

```cpp
cn::openai::tool_registry tools;
auto registered = tools.add({
    .definition = {
        .function_name = "lookup_weather",
        .function_description = "Look up weather by city",
        .function_parameters = {
            {"type", "object"},
            {"properties", {{"city", {{"type", "string"}}}}},
            {"required", {"city"}},
            {"additionalProperties", false},
        },
    },
    .handler = [](const cn::openai::json& arguments)
        -> cn::task<std::expected<cn::openai::json, std::string>> {
        co_return cn::openai::json{{"city", arguments["city"]}, {"temperature", 24}};
    },
});
if (!registered)
    co_return;

cn::openai::conversation_memory memory{{.max_messages = 32}};
cn::openai::openai_chat_model model{client};
cn::openai::agent_executor agent{model, tools, &memory,
    {.max_iterations = 6, .system_prompt = "Use tools when required."}};
agent.with_parallel_tool_execution(ctx);
auto answer = co_await agent.invoke("Shanghai weather?");
```

需要按错误类别定义恢复策略时，在 `agent_options` 中安装异步错误处理器：

```cpp
cn::openai::agent_options options;
options.handle_tool_error = [](const cn::openai::tool_call&,
                                const cn::openai::tool_error& error,
                                const cn::openai::run_config&)
    -> cn::task<cn::openai::tool_error_resolution> {
    if (error.kind == cn::openai::tool_error_kind::invalid_arguments)
        co_return {cn::openai::tool_error_action::return_to_model,
            R"({"recoverable":true,"reason":"invalid arguments"})"};
    co_return {cn::openai::tool_error_action::fail_invocation, error.message};
};
cn::openai::agent_executor governed_agent{model, tools, nullptr,
    std::move(options)};
```

工具集合依赖用户权限或会话状态时，使用动态供应器：

```cpp
cn::openai::functional_tool_provider provider{
    [](const cn::openai::tool_provider_request& request)
        -> cn::task<std::expected<cn::openai::tool_provider_result, std::string>> {
        std::vector<cn::openai::executable_tool> allowed;
        // 根据 request.session_id、request.invocation_parameters 和
        // request.conversation 选择当前一轮允许暴露的工具。
        co_return cn::openai::tool_provider_result{.tools = std::move(allowed)};
    },
    true}; // true 表示每轮模型调用前重新解析

cn::openai::agent_executor agent{model, provider};
auto answer = co_await agent.invoke("Execute the permitted operation", {},
    {.metadata = {{"session_id", "tenant-42"}, {"role", "operator"}}});
```

### 场景：对话记忆与 RAG

数据库消息表实现 `append_only_chat_memory_store` 后，可以直接作为
`conversation_memory` 后端。框架追加时不会先读取或重写历史；批量追加必须在
同一数据库事务中完成。业务分页使用 `count(session_id)` 获取总数，再通过
`load_page(session_id, offset, limit)` 读取稳定插入顺序的页面；模型上下文使用
`load_recent(session_id, limit)` 读取最近窗口：

```cpp
database_chat_store store{/* application repository dependencies */};
cn::openai::conversation_memory memory{"session-42", store,
    {.max_messages = 32, .max_tokens = 8'000}};
co_await memory.append(cn::openai::message::user("Hello"));
auto context_messages = co_await memory.snapshot();

// 只复用框架窗口策略时，无需实现 Store。
auto trimmed = cn::openai::trim_messages(messages,
    {.max_messages = 32,
     .max_tokens = 8'000,
     .pinned_prefix_messages = 1,
     .preserved_tail_messages = 1});
if (!trimmed.limit_satisfied)
    cn::logger::warn{"Protected prompt messages exceed the configured budget"};
```

跨会话用户记忆使用独立的 Long-term Store：

```cpp
cn::openai::in_memory_long_term_store memories{&embeddings};
auto saved = co_await memories.put({"users", user_id}, "preferences",
    cn::openai::json{{"theme", "dark"}, {"language", "zh-CN"}},
    {.ttl = std::chrono::hours{24 * 30}, .expected_version = 0});

auto relevant = co_await memories.search({
    .namespace_prefix = {"users", user_id},
    .query = "preferred response language",
    .limit = 5,
});
```

需要暂停恢复、分支和审计历史的 Agent 使用通用 Checkpointer Adapter：

```cpp
cn::openai::in_memory_checkpoint_store checkpoints;
cn::openai::checkpoint_agentic_scope_store workflow_store{checkpoints};
cn::openai::agentic_runtime runtime{ctx, workflow_store};

auto result = co_await runtime.execute("workflow-42", planner, initial_state);
auto history = co_await checkpoints.list("workflow-42", "main", 20);
auto branch = co_await checkpoints.fork(
    "workflow-42", "main", 2, "experiment");
auto restored = co_await checkpoints.rollback(
    "workflow-42", "main", 1, history->front().version);
```

```cpp
cn::thread_pool cpu_pool{2};
cn::openai::openai_embedding_model embeddings{client};
cn::openai::in_memory_vector_store store{ctx, cpu_pool, embeddings};
co_await store.add_documents({
    {.id = "guide", .page_content = "cnetmod uses C++23 modules."},
});

cn::openai::chat_prompt_template rag_prompt{{
    {.role = "system", .prompt = cn::openai::prompt_template{"Use this context:\n{context}"}},
    {.role = "user", .prompt = cn::openai::prompt_template{"{input}"}},
}};
cn::openai::openai_chat_model model{client};
cn::openai::retrieval_chain rag{store, model, std::move(rag_prompt),
    {.limit = 4, .minimum_score = 0.2F}};
auto result = co_await rag.invoke("How are modules organized?");
```

### 场景：韧性、取消与运行追踪

```cpp
cn::openai::openai_chat_model primary{client};
cn::openai::resilient_chat_model resilient{ctx, primary, {},
    {.max_attempts_per_model = 3,
     .initial_backoff = std::chrono::milliseconds{100},
     .max_backoff = std::chrono::seconds{2}}};

cn::cancel_token cancellation;
cn::openai::run_config config{
    .run_id = "request-42",
    .tags = {"production"},
    .callback = [](const cn::openai::run_event& event) {
        cn::logger::debug{"OpenAI run={} event={} name={}",
            event.run_id, static_cast<int>(event.type), event.name};
    },
    .cancellation = &cancellation,
};
```

### 场景：Embeddings

```cpp
cn::openai::embedding_request req{
    .model = "text-embedding-3-small",
    .input = {"Hello world", "C++ modules"},
    .dimensions = 512,
};
auto resp = co_await client.embeddings(req);
if (resp) {
    for (auto& d : resp->data) {
        cn::logger::info{"Embedding[{}] size={}", d.index, d.embedding.size()};
    }
}
```

### 场景：DALL-E 图片生成

```cpp
cn::openai::image_generation_request req{
    .model = "dall-e-3",
    .prompt = "A futuristic cityscape at sunset",
    .quality = "hd",
    .size = "1792x1024",
};
auto resp = co_await client.create_image(req);
if (resp && !resp->data.empty()) {
    cn::logger::info{"Image URL: {}", resp->data[0].url};
}
```

### 场景：TTS / STT

```cpp
// TTS: 文字转语音
cn::openai::tts_request tts{
    .model = "tts-1",
    .input = "Hello, this is a test.",
    .voice = "alloy",
    .response_format = "mp3",
};
auto audio = co_await client.text_to_speech(tts);

// STT: 语音转文字
cn::openai::transcription_request stt{
    .file = audio_bytes,
    .filename = "audio.mp3",
    .language = "en",
};
auto transcript = co_await client.transcribe(stt);
if (transcript) cn::logger::info{"Text: {}", transcript->text};
```

---

## Part 2: Mail (SMTP)

### 场景导航

- 我要发送邮件 → [看这里](#场景smtp-发送邮件)
- 我要搭建 SMTP 服务端 → [看这里](#场景smtp-服务端)

### API 参考

#### `message` — 邮件消息

**签名**: `export struct message`（`cnetmod::mail` 命名空间）

```cpp
struct message {
    using header = std::pair<std::string, std::string>;
    std::vector<header> headers;
    std::string body;
    void set_header(std::string name, std::string value);
    auto header_value(std::string_view name) const -> std::optional<std::string_view>;
};
```

#### `envelope` — 邮件信封

**签名**: `export struct envelope`

```cpp
struct envelope {
    std::string sender;
    std::vector<std::string> recipients;
    void add_recipient(std::string recipient);
};
```

#### `client` — SMTP 客户端

**签名**: `export class client`（`cnetmod::mail::client`）

```cpp
struct client_options {
    bool tls = false;       // SMTPS (port 465)
    bool starttls = false;  // STARTTLS 升级
    std::string hostname;
    std::uint16_t port = 25;
    bool verify = true;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit client(io_context&, client_options = {}) noexcept` | |
| `connect` | `auto connect(string_view host, uint16_t port = 0) -> task<std::expected<void, std::string>>` | 连接并 EHLO |
| `authenticate` | `auto authenticate(string_view user, string_view pass, auth_mechanism = plain) -> task<...>` | 认证 |
| `send` | `auto send(const envelope&, const message&) -> task<std::expected<void, std::string>>` | 发送邮件 |
| `quit` | `auto quit() -> task<std::expected<void, std::string>>` | 退出 |
| `close` | `void close() noexcept` | 关闭连接 |

支持的认证机制：`plain`, `login`, `cram_md5`, `xoauth2`, `oauthbearer`, `external`

#### `server` — SMTP 服务端

**签名**: `export class server`（`cnetmod::mail::server`）

```cpp
struct server_options {
    std::string hostname = "localhost";
    std::size_t max_message_size = 25U * 1024U * 1024U;
    std::size_t max_recipients = 100;
    bool require_auth = false;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `server(io_context&, server_options = {})` | |
| `listen` | `auto listen(string_view host, uint16_t port) -> std::expected<void, std::error_code>` | 监听端口 |
| `set_message_handler` | `void set_message_handler(recipient_handler)` | 设置消息处理器 |
| `set_authenticator` | `void set_authenticator(authenticator)` | 设置认证回调 |
| `run` | `auto run() -> task<void>` | 启动服务 |
| `stop` | `void stop()` | 停止服务 |

### 场景：SMTP 发送邮件

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.mail;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::mail::client client(ctx, {
        .tls = true,
        .hostname = "smtp.example.com",
        .port = 465,
    });
    co_await client.connect("smtp.example.com", 465);
    co_await client.authenticate("user@example.com", "password");

    cn::mail::envelope env;
    env.sender = "user@example.com";
    env.add_recipient("recipient@example.com");

    cn::mail::message msg;
    msg.set_header("From", "user@example.com");
    msg.set_header("To", "recipient@example.com");
    msg.set_header("Subject", "Hello from cnetmod");
    msg.body = "This is a test email sent via cnetmod SMTP client.";

    auto result = co_await client.send(env, msg);
    if (result) cn::logger::info{"Email sent successfully!"};

    co_await client.quit();
    ctx.stop();
}
```

### 场景：SMTP 服务端

```cpp
auto run_server(cn::io_context& ctx) -> cn::task<void> {
    cn::mail::server server(ctx, {.hostname = "mail.example.com"});
    server.set_message_handler(
        [](const cn::mail::envelope& env, const cn::mail::message& msg)
            -> cn::task<std::expected<void, std::error_code>> {
            cn::logger::info{"Received mail from {} to {}", env.sender, env.recipients[0]};
            co_return std::expected<void, std::error_code>{};
        });
    server.listen("0.0.0.0", 2525);
    co_await server.run();
}
```

---

## Part 3: DNS

### 场景导航

- 我要异步解析域名 → [看这里](#场景dns-客户端查询)
- 我要搭建 DNS 服务端 → [看这里](#场景dns-服务端)
- 我要使用 DoH / DoT → [看这里](#场景doh--dot)

### API 参考

#### DNS 类型

**签名**: `export enum class record_type : std::uint16_t` — `A`(1), `NS`(2), `CNAME`(5), `SOA`(6), `PTR`(12), `MX`(15), `TXT`(16), `AAAA`(28), `SRV`(33), `HTTPS`(65)

**签名**: `export enum class response_code : std::uint8_t` — `no_error`(0), `format_error`(1), `server_failure`(2), `name_error`(3), `refused`(5)

```cpp
export struct question { std::string name; record_type type; record_class cls; };
export struct resource_record { std::string name; record_type type; record_class cls; std::uint32_t ttl; std::vector<std::byte> data; };
export struct message {
    std::uint16_t id;
    bool query; bool recursion_desired;
    response_code rcode;
    std::vector<question> questions;
    std::vector<resource_record> answers;
    std::vector<resource_record> authorities;
    std::vector<resource_record> additionals;
};
```

#### DNS Codec

```cpp
auto parse_message(std::span<const std::byte>) -> std::expected<message, std::error_code>;
auto serialize_message(const message&) -> std::expected<std::vector<std::byte>, std::error_code>;
auto make_query(std::string_view name, record_type, uint16_t id = 0) -> message;
auto a_record(std::string_view name, const ipv4_address&, uint32_t ttl = 60) -> resource_record;
auto aaaa_record(std::string_view name, const ipv6_address&, uint32_t ttl = 60) -> resource_record;
auto txt_record(std::string_view name, std::string_view text, uint32_t ttl = 60) -> std::expected<resource_record, std::error_code>;
auto cname_record(std::string_view name, std::string_view canonical, uint32_t ttl = 60) -> std::expected<resource_record, std::error_code>;
```

#### `udp_client` / `tcp_client` — DNS 客户端

**签名**: `export class udp_client` / `export class tcp_client`（`cnetmod::dns` 命名空间）

| 方法 | 签名 | 说明 |
|------|------|------|
| `udp_client::query` | `auto query(const endpoint& server, const message&) -> task<std::expected<message, std::error_code>>` | UDP 查询 |
| `tcp_client::query` | `auto query(string_view host, uint16_t port, const message&) -> task<...>` | TCP 查询 |

#### `doh_client` / `dot_client` — 加密 DNS

**签名**: `export class doh_client`（DNS over HTTPS）

```cpp
explicit doh_client(io_context&, std::string endpoint_url = "https://dns.google/dns-query");
auto query(const message&) -> task<std::expected<message, std::error_code>>;
```

**签名**: `export class dot_client`（DNS over TLS，需 `CNETMOD_HAS_SSL`）

```cpp
explicit dot_client(io_context&);
auto query(string_view host, uint16_t port, const message&) -> task<...>;
```

#### `udp_server` / `tcp_server` — DNS 服务端

**签名**: `export class udp_server` / `export class tcp_server`（`cnetmod::dns` 命名空间）

| 方法 | 签名 | 说明 |
|------|------|------|
| `listen` | `auto listen(string_view host, uint16_t port, socket_options) -> std::expected<void, std::error_code>` | 监听 |
| `set_handler` | `void set_handler(query_handler)` | 设置查询处理器 |
| `run` | `auto run() -> task<void>` | 启动服务 |
| `stop` | `void stop() noexcept` | 停止 |

`dot_server` 需额外传入 `dot_server_options{.cert_file, .key_file, .verify_peer}`。

### 场景：DNS 客户端查询

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.dns;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::dns::udp_client client(ctx);
    auto server = cn::endpoint{cn::ip_address{cn::ipv4_address{8,8,8,8}}, 53};

    auto query = cn::dns::make_query("example.com", cn::dns::record_type::A, 1);
    auto resp = co_await client.query(server, query);
    if (resp) {
        for (auto& rr : resp->answers) {
            cn::logger::info{"Answer: {} TTL={}", rr.name, rr.ttl};
        }
    }
    ctx.stop();
}
```

### 场景：DoH / DoT

```cpp
// DNS over HTTPS
cn::dns::doh_client doh(ctx, "https://dns.google/dns-query");
auto query = cn::dns::make_query("example.com", cn::dns::record_type::A);
auto resp = co_await doh.query(query);

// DNS over TLS (需 -DCNETMOD_ENABLE_SSL=ON)
cn::dns::dot_client dot(ctx);
auto resp2 = co_await dot.query("dns.google", 853, query);
```

### 场景：DNS 服务端

```cpp
auto run_dns_server(cn::io_context& ctx) -> cn::task<void> {
    cn::dns::udp_server server(ctx);
    server.set_handler([](const cn::dns::message& query, const cn::endpoint& peer)
        -> cn::task<cn::dns::message> {
        cn::dns::message resp;
        resp.id = query.id;
        resp.query = false;
        resp.recursion_desired = true;
        resp.recursion_available = true;
        for (auto& q : query.questions) {
            if (q.type == cn::dns::record_type::A && q.name == "example.com") {
                resp.answers.push_back(
                    cn::dns::a_record("example.com", cn::ipv4_address{93,184,216,34}));
            }
        }
        co_return resp;
    });
    server.listen("0.0.0.0", 5353);
    co_await server.run();
}
```

## Do's & Don'ts

- **Do**: OpenAI 客户端支持自动重连，连接断开后下次调用会自动 reconnect
- **Do**: 优先使用 Responses API 的 `previous_response_id` 延续多轮状态；函数工具结果用 `response_request::tool_outputs`
- **Do**: 流式响应完成或调用方主动停止后会关闭当前连接，下一次请求自动重连，避免残余 chunk 污染后续响应
- **Do**: `extra_body` 仅用于尚未建模的 OpenAI 新字段；稳定字段优先使用强类型成员
- **Do**: SMTP 发送邮件时根据服务端要求选择 `tls`（端口 465）或 `starttls`（端口 587）
- **Do**: DNS 查询使用 `make_query` 构建标准查询，避免手动构造 message
- **Don't**: 不要在 OpenAI `chat_stream` 回调中执行耗时操作，会阻塞 SSE 解析
- **Don't**: DNS `udp_client` 单次查询限制 512 字节，大响应需用 `tcp_client`
