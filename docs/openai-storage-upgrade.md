# OpenAI Storage Upgrade Guide

This release adds two provider-neutral persistence contracts to the OpenAI and
Agent framework. Both are exported by `cnetmod.protocol.openai`; applications
can supply database-backed implementations without changing Agent business
logic.

## 1. General-purpose long-term store

`long_term_store` is intended for cross-session memory and application facts.
It is not a replacement for append-only chat history.

```cpp
import std;
import cnetmod.protocol.openai;

cnetmod::openai::in_memory_long_term_store store{embedding_model};

auto saved = co_await store.put(
    {"tenant", tenant_id, "users"},
    user_id,
    cnetmod::openai::json{{"language", "zh-CN"}},
    {.ttl = std::chrono::hours{24}, .expected_version = std::nullopt});

cnetmod::openai::long_term_search_request request{
    .namespace_prefix = {"tenant", tenant_id},
    .query = "Chinese-speaking users",
    .limit = 20,
    .offset = 0,
};
auto matches = co_await store.search(std::move(request));
```

The contract provides:

- hierarchical namespaces, string keys, and JSON values;
- metadata filtering plus `limit`/`offset` pagination;
- optional TTL and TTL refresh on reads or searches;
- compare-and-swap writes and deletes through `expected_version`;
- optional embedding-based semantic search;
- paginated namespace discovery.

`in_memory_long_term_store` is a coroutine-safe reference implementation for
tests and single-process use. A production Redis, SQL, or document-database
adapter should implement `long_term_store` and preserve the same atomic version
and TTL semantics.

Chat messages continue to use `append_only_chat_memory_store`, whose contract
is `append`/`append_batch`/`load_recent`. This keeps an append-only database as
the single source of truth and avoids whole-conversation rewrites.

## 2. Versioned checkpointer

`checkpoint_store` persists recoverable workflow execution state rather than
user memory. Every committed state is immutable and belongs to a thread and a
branch.

```cpp
import std;
import cnetmod.protocol.openai;

cnetmod::openai::in_memory_checkpoint_store checkpoints;

auto first = co_await checkpoints.commit({
    .thread_id = "order-42",
    .branch = "main",
    .state = cnetmod::openai::json{{"step", "validated"}},
});

auto next = co_await checkpoints.commit({
    .thread_id = "order-42",
    .branch = "main",
    .state = cnetmod::openai::json{{"step", "charged"}},
    .expected_head_version = first->version,
});
```

The contract provides:

- immutable checkpoint versions and paginated history;
- idempotent pending writes identified by write IDs;
- optimistic concurrency for state commits through
  `expected_head_version`;
- optimistic concurrency for pending writes through
  `expected_write_revision`;
- branch creation from any existing version;
- non-destructive rollback, which creates a new head instead of deleting
  history;
- branch-level and thread-level deletion.

The existing Agent runtime can adopt the generic store without changing its
runtime API:

```cpp
cnetmod::openai::in_memory_checkpoint_store checkpoints;
cnetmod::openai::checkpoint_agentic_scope_store agent_store{checkpoints};
// Pass agent_store wherever agentic_scope_store is required.
```

`checkpoint_agentic_scope_store` serializes the Agent scope, planner state,
completed-step count, and pending human-input request into versioned
checkpoints. Concurrent runners cannot silently overwrite one another.

`in_memory_checkpoint_store` is the reference implementation. Durable adapters
must make head comparison and version creation one transaction, and must make
pending-write IDs idempotent.

## Migration guidance

- Keep append-only chat messages in `append_only_chat_memory_store`.
- Put cross-session facts, profiles, and searchable knowledge in
  `long_term_store`.
- Put resumable workflow state, pending side effects, branches, and rollback
  history in `checkpoint_store`.
- Use the in-memory implementations for unit tests; inject durable adapters in
  production.
- Treat version conflicts as concurrency signals to reload and retry, not as
  storage outages.

## Verification

The change was verified with Arch Linux Clang 22 and Windows MSVC Release.
The local non-external suite passed 99 of 99 tests on Linux, and the OpenAI
suite passed all 87 internal cases on Windows. Nine live external-service tests
remain intentionally gated by environment variables.
