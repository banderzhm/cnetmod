module cnetmod.orm.interceptor_chain;

import std;

namespace cnetmod::orm {

struct interceptor_chain::implementation
{
    struct entry
    {
        std::string name;
        int priority{};
        sql_interceptor_function function;
    };

    std::vector<entry> entries;
    bool is_frozen{};
};

interceptor_chain::interceptor_chain()
    : implementation_(std::make_unique<implementation>())
{
}

interceptor_chain::~interceptor_chain() = default;
interceptor_chain::interceptor_chain(interceptor_chain&&) noexcept = default;

auto interceptor_chain::operator=(interceptor_chain&& other) noexcept
    -> interceptor_chain& = default;

auto interceptor_chain::add(std::string name, int priority,
    sql_interceptor_function function) -> std::expected<void, std::string>
{
    if (!implementation_ || implementation_->is_frozen)
        return std::unexpected("interceptor chain is frozen");
    if (name.empty() || !function)
        return std::unexpected("interceptor name and callback are required");
    const auto duplicate = std::ranges::find(implementation_->entries, name,
        &implementation::entry::name);
    if (duplicate != implementation_->entries.end())
        return std::unexpected(std::format("duplicate interceptor '{}'", name));
    implementation_->entries.push_back({std::move(name), priority,
        std::move(function)});
    return {};
}

auto interceptor_chain::freeze() -> std::expected<void, std::string>
{
    if (!implementation_)
        return std::unexpected("interceptor chain is unavailable");
    if (implementation_->is_frozen)
        return {};
    std::ranges::sort(implementation_->entries, {},
        &implementation::entry::priority);
    implementation_->is_frozen = true;
    return {};
}

auto interceptor_chain::frozen() const noexcept -> bool
{
    return implementation_ && implementation_->is_frozen;
}

auto interceptor_chain::empty() const noexcept -> bool
{
    return !implementation_ || implementation_->entries.empty();
}

auto interceptor_chain::apply(sql_operation operation,
    intercepted_statement statement) const
    -> std::expected<intercepted_statement, std::string>
{
    if (!implementation_)
        return std::unexpected("interceptor chain is unavailable");
    if (!implementation_->is_frozen)
        return std::unexpected("interceptor chain must be frozen before use");
    for (const auto& entry : implementation_->entries)
    {
        auto next = entry.function(operation, std::move(statement));
        if (!next)
            return std::unexpected(std::format("interceptor '{}': {}",
                entry.name, next.error()));
        statement = std::move(*next);
    }
    return statement;
}

} // namespace cnetmod::orm
