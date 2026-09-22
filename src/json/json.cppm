/**
 * @brief Backend-neutral JSON document and typed codec facade.
 */
export module cnetmod.json;

import std;

export namespace cnetmod::json {

enum class errc
{
    parse_failed = 1,
    serialization_failed,
    type_mismatch,
    missing_field,
    unknown_field
};

[[nodiscard]] auto make_error_code(errc value) noexcept -> std::error_code;

class document;

/**
 * @brief Structural limits applied before a JSON document is materialized.
 */
struct parse_options
{
    std::size_t max_depth = 256;
    bool reject_duplicate_keys = true;
};

[[nodiscard]] auto parse_document(std::string_view input)
    -> std::expected<document, std::error_code>;
[[nodiscard]] auto parse_document(std::string_view input, parse_options options)
    -> std::expected<document, std::error_code>;
[[nodiscard]] auto write_document(const document& value)
    -> std::expected<std::string, std::error_code>;

/**
 * @brief Backend-neutral JSON value tree.
 *
 * The representation uses only standard C++ types. Parser-specific values,
 * allocators, errors, and headers never cross the module boundary.
 */
class document
{
public:
    using array_type = std::vector<document>;
    using object_type = std::map<std::string, document, std::less<>>;
    using array_t = array_type;
    using object_t = object_type;
    using exception = std::runtime_error;

private:
    using storage_type = std::variant<std::nullptr_t, std::uint64_t,
        std::int64_t, double, std::string, bool, array_type, object_type>;

    template <bool Constant>
    class basic_iterator
    {
        using owner_type = std::conditional_t<Constant, const document, document>;

    public:
        using array_iterator = std::conditional_t<Constant,
            array_type::const_iterator, array_type::iterator>;
        using object_iterator = std::conditional_t<Constant,
            object_type::const_iterator, object_type::iterator>;
        using difference_type = std::ptrdiff_t;
        using value_type = document;
        using reference = std::conditional_t<Constant, const document&, document&>;
        using pointer = std::conditional_t<Constant, const document*, document*>;
        using iterator_category = std::forward_iterator_tag;

        basic_iterator() = default;

        [[nodiscard]] auto operator*() const -> reference
        {
            if (object_)
                return std::get<object_iterator>(position_)->second;
            return *std::get<array_iterator>(position_);
        }

        [[nodiscard]] auto operator->() const -> pointer
        {
            return std::addressof(operator*());
        }

        auto operator++() -> basic_iterator&
        {
            if (object_)
                ++std::get<object_iterator>(position_);
            else
            {
                ++std::get<array_iterator>(position_);
                ++index_;
            }
            return *this;
        }

        auto operator++(int) -> basic_iterator
        {
            auto result = *this;
            ++*this;
            return result;
        }

        [[nodiscard]] friend auto operator==(
            const basic_iterator& left, const basic_iterator& right) -> bool
        {
            if (left.owner_ != right.owner_ || left.object_ != right.object_)
                return false;
            if (left.owner_ == nullptr)
                return true;
            return left.object_
                ? std::get<object_iterator>(left.position_) ==
                    std::get<object_iterator>(right.position_)
                : std::get<array_iterator>(left.position_) ==
                    std::get<array_iterator>(right.position_);
        }

        [[nodiscard]] auto key() const -> std::string
        {
            if (object_)
                return std::get<object_iterator>(position_)->first;
            return std::to_string(index_);
        }

        [[nodiscard]] auto value() const -> reference { return operator*(); }

    private:
        friend class document;

        basic_iterator(owner_type* owner, array_iterator position,
            std::size_t index = 0)
            : owner_(owner), position_(position), index_(index)
        {
        }

        basic_iterator(owner_type* owner, object_iterator position)
            : owner_(owner), position_(position), object_(true)
        {
        }

        owner_type* owner_ = nullptr;
        std::variant<array_iterator, object_iterator> position_{};
        std::size_t index_ = 0;
        bool object_ = false;
    };

public:
    using iterator = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;

    document() = default;
    document(std::nullptr_t) noexcept {}
    document(bool value) : storage_(value) {}
    document(const char* value) : storage_(std::string{value}) {}
    document(std::string value) : storage_(std::move(value)) {}
    document(std::string_view value) : storage_(std::string{value}) {}
    document(array_type value) : storage_(std::move(value)) {}
    document(object_type value) : storage_(std::move(value)) {}

    template <std::integral T>
    requires(!std::same_as<std::remove_cvref_t<T>, bool>)
    document(T value)
    {
        if constexpr (std::is_unsigned_v<T>)
            storage_ = static_cast<std::uint64_t>(value);
        else
            storage_ = static_cast<std::int64_t>(value);
    }

    template <std::floating_point T>
    document(T value) : storage_(static_cast<double>(value))
    {
    }

    template <typename T>
    requires requires(const T& values) {
        typename T::mapped_type;
        typename T::key_type;
        values.begin();
        values.end();
    } && std::convertible_to<typename T::key_type, std::string_view>
    document(const T& values) : storage_(object_type{})
    {
        auto& result = std::get<object_type>(storage_);
        for (const auto& [key, value] : values)
            result.emplace(std::string{key}, document(value));
    }

    template <typename T>
    requires std::ranges::input_range<T> &&
        (!std::convertible_to<T, std::string_view>) &&
        (!std::same_as<std::remove_cvref_t<T>, array_type>) &&
        (!std::same_as<std::remove_cvref_t<T>, object_type>) &&
        (!requires { typename T::mapped_type; })
    document(const T& values) : storage_(array_type{})
    {
        auto& result = std::get<array_type>(storage_);
        if constexpr (requires { std::ranges::size(values); })
            result.reserve(std::ranges::size(values));
        for (const auto& value : values)
            result.emplace_back(value);
    }

    document(std::initializer_list<std::pair<const char*, document>> values)
        : storage_(object_type{})
    {
        auto& result = std::get<object_type>(storage_);
        for (const auto& [key, value] : values)
            result.emplace(key, value);
    }

    template <bool DeprioritizeObject = true>
    document(std::initializer_list<document> values)
        : storage_(array_type(values))
    {
    }

    [[nodiscard]] static auto object() -> document { return document(object_type{}); }
    [[nodiscard]] static auto object(
        std::initializer_list<std::pair<const char*, document>> values) -> document
    {
        return document(values);
    }
    [[nodiscard]] static auto array() -> document { return document(array_type{}); }
    [[nodiscard]] static auto array(std::initializer_list<document> values) -> document
    {
        return document(array_type(values));
    }

    [[nodiscard]] static auto parse(std::string_view input,
        std::nullptr_t = nullptr, bool allow_exceptions = true,
        bool ignore_comments = false) -> document;
    [[nodiscard]] static auto parse(std::istream& input,
        std::nullptr_t = nullptr, bool allow_exceptions = true,
        bool ignore_comments = false) -> document;
    [[nodiscard]] auto dump(int indentation = -1) const -> std::string;
    [[nodiscard]] auto is_discarded() const noexcept -> bool { return discarded_; }

    [[nodiscard]] auto is_null() const noexcept -> bool { return holds<std::nullptr_t>(); }
    [[nodiscard]] auto is_boolean() const noexcept -> bool { return holds<bool>(); }
    [[nodiscard]] auto is_string() const noexcept -> bool { return holds<std::string>(); }
    [[nodiscard]] auto is_array() const noexcept -> bool { return holds<array_type>(); }
    [[nodiscard]] auto is_object() const noexcept -> bool { return holds<object_type>(); }
    [[nodiscard]] auto is_number_unsigned() const noexcept -> bool { return holds<std::uint64_t>(); }
    [[nodiscard]] auto is_number_integer() const noexcept -> bool
    {
        return holds<std::uint64_t>() || holds<std::int64_t>();
    }
    [[nodiscard]] auto is_number_float() const noexcept -> bool { return holds<double>(); }
    [[nodiscard]] auto is_number() const noexcept -> bool
    {
        return is_number_integer() || is_number_float();
    }
    [[nodiscard]] auto is_structured() const noexcept -> bool
    {
        return is_array() || is_object();
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        if (const auto* value = std::get_if<array_type>(&storage_)) return value->empty();
        if (const auto* value = std::get_if<object_type>(&storage_)) return value->empty();
        if (const auto* value = std::get_if<std::string>(&storage_)) return value->empty();
        return is_null();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        if (const auto* value = std::get_if<array_type>(&storage_)) return value->size();
        if (const auto* value = std::get_if<object_type>(&storage_)) return value->size();
        if (const auto* value = std::get_if<std::string>(&storage_)) return value->size();
        return 0;
    }

    [[nodiscard]] auto contains(std::string_view key) const -> bool
    {
        const auto* object = std::get_if<object_type>(&storage_);
        return object && object->contains(key);
    }

    auto operator[](std::string_view key) -> document&
    {
        if (is_null()) storage_ = object_type{};
        return std::get<object_type>(storage_)[std::string{key}];
    }
    [[nodiscard]] auto operator[](std::string_view key) const -> const document&
    {
        return std::get<object_type>(storage_).at(std::string{key});
    }
    auto operator[](std::size_t index) -> document& { return std::get<array_type>(storage_)[index]; }
    [[nodiscard]] auto operator[](std::size_t index) const -> const document&
    {
        return std::get<array_type>(storage_)[index];
    }
    auto at(std::string_view key) -> document&
    {
        return std::get<object_type>(storage_).at(std::string{key});
    }
    [[nodiscard]] auto at(std::string_view key) const -> const document&
    {
        return std::get<object_type>(storage_).at(std::string{key});
    }
    auto at(std::size_t index) -> document&
    {
        return std::get<array_type>(storage_).at(index);
    }
    [[nodiscard]] auto at(std::size_t index) const -> const document&
    {
        return std::get<array_type>(storage_).at(index);
    }
    auto at(int index) -> document&
    {
        return at(static_cast<std::size_t>(index));
    }
    [[nodiscard]] auto at(int index) const -> const document&
    {
        return at(static_cast<std::size_t>(index));
    }

    void push_back(document value)
    {
        if (is_null()) storage_ = array_type{};
        std::get<array_type>(storage_).push_back(std::move(value));
    }
    void pop_back() { std::get<array_type>(storage_).pop_back(); }
    auto emplace(std::string key, document value) -> std::pair<object_type::iterator, bool>
    {
        if (is_null()) storage_ = object_type{};
        return std::get<object_type>(storage_).emplace(std::move(key), std::move(value));
    }
    void clear()
    {
        if (auto* value = std::get_if<array_type>(&storage_)) value->clear();
        else if (auto* value = std::get_if<object_type>(&storage_)) value->clear();
        else if (auto* value = std::get_if<std::string>(&storage_)) value->clear();
        else storage_ = nullptr;
    }

    [[nodiscard]] auto begin() -> iterator
    {
        if (auto* object = std::get_if<object_type>(&storage_)) return iterator{this, object->begin()};
        if (auto* array = std::get_if<array_type>(&storage_))
            return iterator{this, array->begin()};
        return {};
    }
    [[nodiscard]] auto end() -> iterator
    {
        if (auto* object = std::get_if<object_type>(&storage_)) return iterator{this, object->end()};
        if (auto* array = std::get_if<array_type>(&storage_))
            return iterator{this, array->end(), size()};
        return {};
    }
    [[nodiscard]] auto begin() const -> const_iterator
    {
        if (const auto* object = std::get_if<object_type>(&storage_)) return const_iterator{this, object->begin()};
        if (const auto* array = std::get_if<array_type>(&storage_))
            return const_iterator{this, array->begin()};
        return {};
    }
    [[nodiscard]] auto end() const -> const_iterator
    {
        if (const auto* object = std::get_if<object_type>(&storage_)) return const_iterator{this, object->end()};
        if (const auto* array = std::get_if<array_type>(&storage_))
            return const_iterator{this, array->end(), size()};
        return {};
    }
    [[nodiscard]] auto find(std::string_view key) -> iterator
    {
        auto* object = std::get_if<object_type>(&storage_);
        return object ? iterator{this, object->find(key)} : end();
    }
    [[nodiscard]] auto find(std::string_view key) const -> const_iterator
    {
        const auto* object = std::get_if<object_type>(&storage_);
        return object ? const_iterator{this, object->find(key)} : end();
    }
    auto erase(iterator position) -> iterator
    {
        if (auto* object = std::get_if<object_type>(&storage_))
            return iterator{this, object->erase(std::get<typename iterator::object_iterator>(position.position_))};
        auto& array = std::get<array_type>(storage_);
        return iterator{this, array.erase(std::get<typename iterator::array_iterator>(position.position_))};
    }
    auto erase(std::string_view key) -> std::size_t
    {
        auto* object = std::get_if<object_type>(&storage_);
        return object ? object->erase(std::string{key}) : 0;
    }

    template <typename T>
    [[nodiscard]] auto get() const -> T
    {
        if constexpr (std::same_as<T, document>) return *this;
        else if constexpr (std::same_as<T, std::string>) return std::get<std::string>(storage_);
        else if constexpr (std::same_as<T, bool>) return std::get<bool>(storage_);
        else if constexpr (std::integral<T> && std::is_unsigned_v<T>)
        {
            if (const auto* value = std::get_if<std::uint64_t>(&storage_)) return static_cast<T>(*value);
            if (const auto* value = std::get_if<std::int64_t>(&storage_)) return static_cast<T>(*value);
            return static_cast<T>(std::get<double>(storage_));
        }
        else if constexpr (std::integral<T>)
        {
            if (const auto* value = std::get_if<std::int64_t>(&storage_)) return static_cast<T>(*value);
            if (const auto* value = std::get_if<std::uint64_t>(&storage_)) return static_cast<T>(*value);
            return static_cast<T>(std::get<double>(storage_));
        }
        else if constexpr (requires(T result, typename T::value_type value) {
            result.push_back(std::move(value));
        })
        {
            T result;
            for (const auto& value : std::get<array_type>(storage_))
                result.push_back(value.template get<typename T::value_type>());
            return result;
        }
        else if constexpr (requires {
            typename T::mapped_type;
            typename T::key_type;
        })
        {
            T result;
            for (const auto& [key, value] : std::get<object_type>(storage_))
                result.emplace(key, value.template get<typename T::mapped_type>());
            return result;
        }
        else if constexpr (std::same_as<T, std::filesystem::path>)
            return std::filesystem::path{std::get<std::string>(storage_)};
        else if constexpr (std::floating_point<T>)
        {
            if (const auto* value = std::get_if<double>(&storage_)) return static_cast<T>(*value);
            if (const auto* value = std::get_if<std::int64_t>(&storage_)) return static_cast<T>(*value);
            return static_cast<T>(std::get<std::uint64_t>(storage_));
        }
        else
            static_assert(std::is_void_v<T>, "unsupported JSON conversion");
    }

    template <typename T>
    [[nodiscard]] auto get_ref() -> T
    {
        using value_type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<value_type, std::string>) return std::get<std::string>(storage_);
        else if constexpr (std::same_as<value_type, array_type>) return std::get<array_type>(storage_);
        else return std::get<object_type>(storage_);
    }
    template <typename T>
    [[nodiscard]] auto get_ref() const -> T
    {
        using value_type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<value_type, std::string>) return std::get<std::string>(storage_);
        else if constexpr (std::same_as<value_type, array_type>) return std::get<array_type>(storage_);
        else return std::get<object_type>(storage_);
    }

    template <typename T>
    [[nodiscard]] auto value(std::string_view key, T fallback) const -> T
    {
        const auto found = find(key);
        return found == end() || found->is_null() ? std::move(fallback) : found->template get<T>();
    }
    template <std::size_t N>
    [[nodiscard]] auto value(std::string_view key, const char (&fallback)[N]) const -> std::string
    {
        return value(key, std::string{fallback});
    }

    [[nodiscard]] friend auto operator==(
        const document& left, const document& right) -> bool
    {
        if (left.is_number() && right.is_number())
        {
            if (left.is_number_float() || right.is_number_float())
                return left.get<long double>() == right.get<long double>();
            if (left.is_number_unsigned() && right.is_number_unsigned())
                return left.get<std::uint64_t>() == right.get<std::uint64_t>();
            if (!left.is_number_unsigned() && !right.is_number_unsigned())
                return left.get<std::int64_t>() == right.get<std::int64_t>();
            const auto& unsigned_value = left.is_number_unsigned() ? left : right;
            const auto& signed_value = left.is_number_unsigned() ? right : left;
            const auto signed_number = signed_value.get<std::int64_t>();
            return signed_number >= 0 && unsigned_value.get<std::uint64_t>() ==
                static_cast<std::uint64_t>(signed_number);
        }
        return left.storage_ == right.storage_ && left.discarded_ == right.discarded_;
    }
    [[nodiscard]] friend auto operator==(const document& left,
        std::string_view right) -> bool
    {
        return left.is_string() && left.get<std::string>() == right;
    }
    [[nodiscard]] friend auto operator==(const document& left,
        const std::string& right) -> bool
    {
        return left == std::string_view{right};
    }
    [[nodiscard]] friend auto operator==(const std::string& left,
        const document& right) -> bool
    {
        return right == left;
    }
    [[nodiscard]] friend auto operator==(const document& left,
        const char* right) -> bool
    {
        return left == std::string_view{right};
    }
    [[nodiscard]] friend auto operator==(const char* left,
        const document& right) -> bool
    {
        return right == left;
    }
    friend auto operator<<(std::ostream& output, const document& value)
        -> std::ostream&
    {
        return output << value.dump();
    }
    [[nodiscard]] explicit operator bool() const noexcept { return !is_null(); }

private:
    template <typename T>
    [[nodiscard]] auto holds() const noexcept -> bool { return std::holds_alternative<T>(storage_); }

    storage_type storage_ = nullptr;
    bool discarded_ = false;
};

using value = document;
using raw_value = document;

template <typename Owner, typename Member>
struct field_descriptor
{
    std::string_view name;
    Member Owner::* member;
};

template <typename Owner, typename Member>
[[nodiscard]] constexpr auto field(std::string_view name, Member Owner::* member)
    -> field_descriptor<Owner, Member>
{
    return {name, member};
}

template <typename T>
struct document_traits;

template <typename T>
concept FieldDocumentMapped = requires { document_traits<T>::fields(); };

template <typename T>
concept CustomDocumentMapped = requires(
    const T& value, const document& source, bool option) {
    { document_traits<T>::encode(value, option) }
        -> std::same_as<std::expected<document, std::error_code>>;
    { document_traits<T>::decode(source, option) }
        -> std::same_as<std::expected<T, std::error_code>>;
};

template <typename T>
concept DocumentMapped = FieldDocumentMapped<T> || CustomDocumentMapped<T>;

namespace detail {
template <typename T> struct is_optional : std::false_type {};
template <typename T> struct is_optional<std::optional<T>> : std::true_type { using value_type = T; };
template <typename T> inline constexpr bool is_optional_v = is_optional<std::remove_cvref_t<T>>::value;
template <typename T> concept StringLike = std::convertible_to<T, std::string_view>;
template <typename T>
concept AssociativeObject = requires {
    typename T::key_type;
    typename T::mapped_type;
} && std::convertible_to<typename T::key_type, std::string_view>;
template <typename T>
concept Sequence = std::ranges::input_range<T> && !StringLike<T> &&
    (!AssociativeObject<T>);

template <typename T>
[[nodiscard]] auto encode_value(const T& value, bool emit_nulls)
    -> std::expected<document, std::error_code>;
template <typename T>
[[nodiscard]] auto decode_value(const document& source, bool reject_unknown)
    -> std::expected<T, std::error_code>;

template <typename T>
[[nodiscard]] auto encode_object(const T& value, bool emit_nulls)
    -> std::expected<document, std::error_code>
{
    auto result = document::object();
    auto status = std::expected<void, std::error_code>{};
    std::apply([&](const auto&... member) {
        ([&] {
            if (!status) return;
            const auto& field_value = value.*(member.member);
            if constexpr (is_optional_v<decltype(field_value)>)
                if (!field_value && !emit_nulls) return;
            auto encoded = encode_value(field_value, emit_nulls);
            if (!encoded) status = std::unexpected(encoded.error());
            else result[member.name] = std::move(*encoded);
        }(), ...);
    }, document_traits<T>::fields());
    if (!status) return std::unexpected(status.error());
    return result;
}

template <typename T>
[[nodiscard]] auto decode_object(const document& source, bool reject_unknown)
    -> std::expected<T, std::error_code>
{
    if (!source.is_object()) return std::unexpected(make_error_code(errc::type_mismatch));
    T result{};
    std::size_t known = 0;
    auto status = std::expected<void, std::error_code>{};
    std::apply([&](const auto&... member) {
        ([&] {
            if (!status) return;
            const auto found = source.find(member.name);
            using member_type = std::remove_cvref_t<decltype(result.*(member.member))>;
            if (found == source.end())
            {
                if constexpr (!is_optional_v<member_type>)
                    status = std::unexpected(make_error_code(errc::missing_field));
                return;
            }
            ++known;
            auto decoded = decode_value<member_type>(*found, reject_unknown);
            if (!decoded) status = std::unexpected(decoded.error());
            else result.*(member.member) = std::move(*decoded);
        }(), ...);
    }, document_traits<T>::fields());
    if (!status) return std::unexpected(status.error());
    if (reject_unknown && known != source.size())
        return std::unexpected(make_error_code(errc::unknown_field));
    return result;
}

template <typename T>
[[nodiscard]] auto encode_value(const T& value, bool emit_nulls)
    -> std::expected<document, std::error_code>
{
    using value_type = std::remove_cvref_t<T>;
    if constexpr (std::same_as<value_type, document>) return value;
    else if constexpr (is_optional_v<value_type>)
        return value ? encode_value(*value, emit_nulls)
                     : std::expected<document, std::error_code>{document(nullptr)};
    else if constexpr (std::same_as<value_type, bool> || std::integral<value_type> ||
        std::floating_point<value_type> || StringLike<value_type>) return document(value);
    else if constexpr (std::is_enum_v<value_type>)
        return document(static_cast<std::underlying_type_t<value_type>>(value));
    else if constexpr (CustomDocumentMapped<value_type>)
        return document_traits<value_type>::encode(value, emit_nulls);
    else if constexpr (FieldDocumentMapped<value_type>)
        return encode_object(value, emit_nulls);
    else if constexpr (AssociativeObject<value_type>)
    {
        auto result = document::object();
        for (const auto& [key, entry] : value)
        {
            auto encoded = encode_value(entry, emit_nulls);
            if (!encoded) return std::unexpected(encoded.error());
            result[std::string_view{key}] = std::move(*encoded);
        }
        return result;
    }
    else if constexpr (Sequence<value_type>)
    {
        auto result = document::array();
        for (const auto& entry : value)
        {
            auto encoded = encode_value(entry, emit_nulls);
            if (!encoded) return std::unexpected(encoded.error());
            result.push_back(std::move(*encoded));
        }
        return result;
    }
    else return std::unexpected(make_error_code(errc::type_mismatch));
}

template <typename T>
[[nodiscard]] auto decode_value(const document& source, bool reject_unknown)
    -> std::expected<T, std::error_code>
{
    using value_type = std::remove_cvref_t<T>;
    try
    {
        if constexpr (std::same_as<value_type, document>) return source;
        else if constexpr (is_optional_v<value_type>)
        {
            if (source.is_null()) return value_type{};
            auto decoded = decode_value<typename is_optional<value_type>::value_type>(source, reject_unknown);
            if (!decoded) return std::unexpected(decoded.error());
            return value_type{std::move(*decoded)};
        }
        else if constexpr (std::same_as<value_type, std::string>)
        {
            if (!source.is_string()) return std::unexpected(make_error_code(errc::type_mismatch));
            return source.template get<std::string>();
        }
        else if constexpr (std::same_as<value_type, bool>)
        {
            if (!source.is_boolean()) return std::unexpected(make_error_code(errc::type_mismatch));
            return source.template get<bool>();
        }
        else if constexpr (std::integral<value_type>)
        {
            if constexpr (std::is_unsigned_v<value_type>)
            {
                if (source.is_number_unsigned())
                {
                    const auto value = source.template get<std::uint64_t>();
                    if (value <= std::numeric_limits<value_type>::max())
                        return static_cast<value_type>(value);
                }
                else if (source.is_number_integer())
                {
                    const auto value = source.template get<std::int64_t>();
                    if (value >= 0 && static_cast<std::uint64_t>(value) <=
                            std::numeric_limits<value_type>::max())
                        return static_cast<value_type>(value);
                }
            }
            else
            {
                if (source.is_number_unsigned())
                {
                    const auto value = source.template get<std::uint64_t>();
                    if (value <= static_cast<std::uint64_t>(
                            std::numeric_limits<value_type>::max()))
                        return static_cast<value_type>(value);
                }
                else if (source.is_number_integer())
                {
                    const auto value = source.template get<std::int64_t>();
                    if (value >= std::numeric_limits<value_type>::min() &&
                        value <= std::numeric_limits<value_type>::max())
                        return static_cast<value_type>(value);
                }
            }
            return std::unexpected(make_error_code(errc::type_mismatch));
        }
        else if constexpr (std::floating_point<value_type>)
        {
            if (!source.is_number())
                return std::unexpected(make_error_code(errc::type_mismatch));
            return source.template get<value_type>();
        }
        else if constexpr (std::is_enum_v<value_type>)
        {
            auto decoded = decode_value<std::underlying_type_t<value_type>>(source, reject_unknown);
            if (!decoded) return std::unexpected(decoded.error());
            return static_cast<value_type>(*decoded);
        }
        else if constexpr (CustomDocumentMapped<value_type>)
            return document_traits<value_type>::decode(source, reject_unknown);
        else if constexpr (FieldDocumentMapped<value_type>)
            return decode_object<value_type>(source, reject_unknown);
        else if constexpr (AssociativeObject<value_type>)
        {
            if (!source.is_object())
                return std::unexpected(make_error_code(errc::type_mismatch));
            value_type result;
            for (auto iterator = source.begin(); iterator != source.end(); ++iterator)
            {
                auto decoded = decode_value<typename value_type::mapped_type>(
                    iterator.value(), reject_unknown);
                if (!decoded) return std::unexpected(decoded.error());
                result.emplace(iterator.key(), std::move(*decoded));
            }
            return result;
        }
        else if constexpr (Sequence<value_type>)
        {
            if (!source.is_array()) return std::unexpected(make_error_code(errc::type_mismatch));
            value_type result;
            for (const auto& entry : source)
            {
                auto decoded = decode_value<typename value_type::value_type>(entry, reject_unknown);
                if (!decoded) return std::unexpected(decoded.error());
                result.push_back(std::move(*decoded));
            }
            return result;
        }
        else return std::unexpected(make_error_code(errc::type_mismatch));
    }
    catch (...) { return std::unexpected(make_error_code(errc::type_mismatch)); }
}
} // namespace detail

/**
 * @brief Converts a typed value into the backend-neutral document tree.
 */
template <typename T>
[[nodiscard]] auto to_document(const T& value, bool emit_nulls = false)
    -> std::expected<document, std::error_code>
{
    return detail::encode_value(value, emit_nulls);
}

/**
 * @brief Converts a document tree into a typed value.
 */
template <typename T>
[[nodiscard]] auto from_document(
    const document& source, bool reject_unknown = true)
    -> std::expected<T, std::error_code>
{
    return detail::decode_value<T>(source, reject_unknown);
}

struct default_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input) -> std::expected<T, std::error_code>
    {
        auto parsed = parse_document(input);
        if (!parsed) return std::unexpected(parsed.error());
        return detail::decode_value<T>(*parsed, true);
    }
    template <typename T>
    [[nodiscard]] static auto encode(const T& value) -> std::expected<std::string, std::error_code>
    {
        auto result = detail::encode_value(value, false);
        if (!result) return std::unexpected(result.error());
        return write_document(*result);
    }
};

struct lenient_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input) -> std::expected<T, std::error_code>
    {
        auto parsed = parse_document(input);
        if (!parsed) return std::unexpected(parsed.error());
        return detail::decode_value<T>(*parsed, false);
    }
    template <typename T>
    [[nodiscard]] static auto encode(const T& value) -> std::expected<std::string, std::error_code>
    {
        return default_codec::encode(value);
    }
};

struct explicit_null_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input) -> std::expected<T, std::error_code>
    {
        return default_codec::decode<T>(input);
    }
    template <typename T>
    [[nodiscard]] static auto encode(const T& value) -> std::expected<std::string, std::error_code>
    {
        auto result = detail::encode_value(value, true);
        if (!result) return std::unexpected(result.error());
        return write_document(*result);
    }
};

template <typename Codec, typename T>
concept codec_for = requires(std::string_view input, const T& value) {
    { Codec::template decode<T>(input) } -> std::same_as<std::expected<T, std::error_code>>;
    { Codec::template encode<T>(value) } -> std::same_as<std::expected<std::string, std::error_code>>;
};

template <typename T, typename Codec = default_codec>
requires codec_for<Codec, T>
[[nodiscard]] auto parse(std::string_view input) -> std::expected<T, std::error_code>
{
    return Codec::template decode<T>(input);
}

template <typename T, typename Codec = default_codec>
requires codec_for<Codec, T>
[[nodiscard]] auto write(const T& value) -> std::expected<std::string, std::error_code>
{
    return Codec::template encode<T>(value);
}

} // namespace cnetmod::json

export template <>
struct std::is_error_code_enum<cnetmod::json::errc> : std::true_type
{
};
