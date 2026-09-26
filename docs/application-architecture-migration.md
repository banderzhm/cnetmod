# Application architecture migration (modules, components, options)

This release replaces the callback-based composition API. There is no
compatibility layer; every item below is a compile-time break with a direct
replacement.

## Builder

| Removed | Replacement |
|---|---|
| `builder.routes(route_configurer)` / `routes(runtime_route_configurer)` | `application_module::compose()` → `composition_context::routes` |
| `builder.middleware(fn)` / `runtime_middleware(factory)` | `composition_context::middleware.push_back(fn)` |
| `build() -> expected<host, error_code>` | `build() -> expected<host, build_error>`; `error().code` keeps the code, `describe()` prints phase, component and path |
| `application_middleware`, `route_configurer`, `runtime_route_configurer`, `runtime_middleware_factory` | removed |

`service()`, `service_factory()`, `configure()`, `configuration_file()` and
`enable_auto_configuration()` are unchanged. `add_module()` / `add_module<M>()`
adds modules; `make_module(name, module_hooks{...})` defines one from callbacks.

```cpp
// before
builder.routes([](http::router& routes, application_runtime& runtime) { ... })
       .runtime_middleware([](application_runtime& runtime) { return auth(runtime); });

// after
builder.add_module(application::make_module("web", {
    .compose = [](application::composition_context& context)
        -> std::expected<void, std::string> {
        context.middleware.push_back(auth(context.runtime));
        context.routes.get("/orders", list_orders);
        return {};
    }}));
```

Middleware now composes after managed services and components exist, so
authentication middleware can capture repositories directly; deferred binding
workarounds are no longer needed.

## Runtime

| Removed from `application_runtime` | Replacement |
|---|---|
| `repository<T>(instance, policies, provider)` | `add_repository<T>(components, {.instance, .policies, .provider})`, inject `managed_repository<T>` |
| `repository<T>(request, ...)` | inject `repository_factory<T>`, call `for_request(request)` |
| `require_tenant_scope(bool)` | configuration `orm.tenant_scope_required` (frozen at build) |
| `redis(instance, options)` | inject `redis_service` by instance name, call `make_template(options)` |
| `chat_model(instance, options)` | inject `chat_model_service`, call `make_template(options)`; or `add_chat_model()` |
| `reconfigure_chat_model(...)` | `chat_model_service::reconfigure(...)` |
| `sign_jwt(...)` / `verify_jwt(...)` | `security::sign_jwt/verify_jwt(executor.cpu_pool(), executor.event_loop(), ...)` |
| constructor parameter `service_registry&` | removed |

New: `runtime.executor()` returns `execution_context` (event loop, CPU pool,
`post()`, `sleep()`, `with_timeout()`).

## Configuration

- `load_configuration`, `validate_configuration`, `reload_safe_configuration`
  and `application_host::reload_configuration` return `configuration_error`
  (`code`, `path`, `message`) instead of `std::error_code`.
- Unknown top-level sections are no longer rejected by the loader; they must be
  declared with `options_registry::section<T>()` or the build fails in the
  `options` phase with the section name as path.
- `${NAME:-default}` and the `$${` escape are supported.
- `orm.tenant_scope_required` is a new key.

## HTTP authentication and authorization

| Removed | Replacement |
|---|---|
| `jwt_auth_options::skip_paths` | declare `http::allow_anonymous` / `http::optional_authentication` on the route |
| `authorization_options::skip` | `http::allow_anonymous` on the route |
| mandatory `authorization_options::requirement_for` | optional; default reads `http::required_permissions` from the route |

Behavior changes to review:

- Requests that match no route pass authentication and authorization so the
  router answers 404 (`jwt_auth_options::unmatched`,
  `authorization_options::authorize_unmatched` restore enforcement).
- On optional routes a presented but invalid credential is rejected with 401;
  set `invalid_optional = invalid_optional_credentials::continue_anonymous` for
  the previous behavior.
- `authorize()` throws `std::invalid_argument` without an authenticator and
  answers 503 for `authorization_error_code::verifier_failure`.

Router registration functions take an optional trailing `endpoint_metadata`
(after the options argument for `stream_*` and `sse_*`).

## Chat model template

`chat_model_template` derives from `ai::chat_model`. The `chat_request`
overloads of `invoke` and `stream` take `const ai::run_config&`. Code that
passed temporaries is unaffected.

## ORM

`repository_impl` caches its interceptor chain; its constructor is no longer
`noexcept`. `mapper<T>::configure(options, cache)` installs a cached chain.
