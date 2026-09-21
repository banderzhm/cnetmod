# MySQL transactions

Use the Application Repository for ORM transactions. It owns the pool lease and guarantees that every Mapper in the callback uses one MySQL connection from begin through commit or rollback.

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

A successful `expected` commits. An error or exception rolls back. Do not start a nested Repository transaction, perform unrelated remote calls while holding the lease, or retain a Mapper after the callback.

Raw protocol code may use the transaction API exported by `cnetmod.protocol.mysql`, but it is not the ORM application path. See [`advanced/orm-guide.md`](advanced/orm-guide.md).
