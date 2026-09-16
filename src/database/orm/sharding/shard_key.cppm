/**
 * @brief Stable shard-key representation independent of database protocols.
 */
export module cnetmod.orm.sharding.shard_key;

import std;

export namespace cnetmod::orm {

/**
 * @brief Value used to route one operation to a physical database and table.
 */
class shard_key
{
public:
    /**
     * @brief Creates a shard key from any non-boolean integral value.
     */
    template <std::integral Integer>
    requires(!std::same_as<std::remove_cv_t<Integer>, bool>)
    shard_key(Integer value) noexcept
    {
        if constexpr (std::signed_integral<Integer>)
            value_ = static_cast<std::int64_t>(value);
        else
            value_ = static_cast<std::uint64_t>(value);
    }

    /**
     * @brief Creates a shard key by taking ownership of a string value.
     */
    shard_key(std::string value) : value_(std::move(value)) {}

    /**
     * @brief Creates a shard key by copying a string view.
     */
    shard_key(std::string_view value) : value_(std::string{value}) {}

    /**
     * @brief Creates a shard key from a null-safe C string.
     */
    shard_key(const char* value) : value_(std::string{value ? value : ""}) {}

    /**
     * @brief Returns a deterministic FNV-1a hash on every supported platform.
     */
    [[nodiscard]] auto stable_hash() const noexcept -> std::uint64_t
    {
        constexpr std::uint64_t offset = 14695981039346656037ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;
        auto append = [](std::uint64_t hash, std::byte byte) noexcept
        {
            return (hash ^ static_cast<std::uint64_t>(byte)) * prime;
        };
        return std::visit([&](const auto& value) noexcept
            {
                using value_type = std::remove_cvref_t<decltype(value)>;
                std::uint64_t hash = offset;
                if constexpr (std::same_as<value_type, std::string>)
                {
                    for (const auto character : value)
                        hash = append(hash, static_cast<std::byte>(static_cast<unsigned char>(character)));
                }
                else
                {
                    using unsigned_type = std::make_unsigned_t<value_type>;
                    auto number = static_cast<unsigned_type>(value);
                    for (std::size_t index = 0; index < sizeof(number); ++index)
                    {
                        hash = append(hash, static_cast<std::byte>(number & 0xffU));
                        number >>= 8U;
                    }
                }
                return hash;
            },
            value_);
    }

    /**
     * @brief Reports whether a textual shard key is empty.
     */
    [[nodiscard]] auto empty() const noexcept -> bool
    {
        const auto* text = std::get_if<std::string>(&value_);
        return text != nullptr && text->empty();
    }

private:
    std::variant<std::uint64_t, std::int64_t, std::string> value_;
};

} // namespace cnetmod::orm
