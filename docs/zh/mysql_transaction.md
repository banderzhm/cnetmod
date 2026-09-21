# MySQL 事务

ORM 事务统一使用 Application Repository。Repository 持有连接池租约，并保证回调中的所有 Mapper 从开始到提交或回滚都使用同一个 MySQL 连接。

```cpp
auto accounts = host->runtime().repository<account>("primary", {},
    cnetmod::application::database_provider::mysql);
if (!accounts)
    co_return;

auto outcome = co_await accounts->transaction<void>(
    [&](auto& unit) -> cnetmod::task<std::expected<void, std::string>>
    {
        auto account_mapper = unit.template mapper<account>();
        auto ledger_mapper = unit.template mapper<ledger_entry>();

        auto updated = co_await account_mapper.update_by_id(account_value);
        if (updated.is_err())
            co_return std::unexpected(updated.error_msg);

        auto inserted = co_await ledger_mapper.insert(entry);
        if (inserted.is_err())
            co_return std::unexpected(inserted.error_msg);

        co_return {};
    });
```

成功的 `expected` 会提交；错误或异常会回滚。不要嵌套 Repository 事务，不要在持有连接期间调用无关远程服务，也不要在回调结束后保留 Mapper。

底层协议代码仍可使用 `cnetmod.protocol.mysql` 导出的原始事务 API，但它不是 ORM 应用入口。参见 [`advanced/orm-guide.md`](advanced/orm-guide.md)。
