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
    bool reject_duplicate_keys = false;
};

[[nodiscard]] auto parse_document(std::string_view input)
    -> std::expected<document, std::error_code>;
[[nodiscard]] auto parse_document(std::string_view input, parse_options options)
    -> std::expected<document, std::error_code>;
[[nodiscard]] auto write_document(const document& value, bool prettify = false)
    -> std::expected<std::string, std::error_code>;

namespace detail {
    /// Append-only output for the direct writer: bytes are written through a
    /// raw pointer into reserved string storage, so each byte costs no size
    /// check or out-of-line call; capacity grows geometrically.
    class json_writer
    {
    public:
        // Member functions defined in a named module's class are not
        // implicitly inline, so the per-byte primitives say so explicitly;
        // otherwise every put/append is an out-of-line call.
        inline explicit json_writer(std::size_t initial = 256U)
        {
            resize_uninitialized(initial);
        }

        inline explicit json_writer(std::string& output,
            std::size_t initial = 256U)
            : output_(&output)
        {
            output.clear();
            resize_uninitialized(std::max(initial, output.capacity()));
        }

        [[nodiscard]] inline auto size() const noexcept -> std::size_t { return size_; }

        inline void reserve(std::size_t extra)
        {
            if (size_ + extra <= capacity_) [[likely]]
                return;
            grow(extra);
        }

        inline void put(char value)
        {
            reserve(1U);
            data_[size_++] = value;
        }

        inline void put_unchecked(char value) noexcept { data_[size_++] = value; }

        inline void append(const char* value, std::size_t count)
        {
            reserve(count);
            std::memcpy(data_ + size_, value, count);
            size_ += count;
        }

        inline void append(std::string_view value) { append(value.data(), value.size()); }

        inline void append_unchecked(const char* value, std::size_t count) noexcept
        {
            std::memcpy(data_ + size_, value, count);
            size_ += count;
        }

        /// Write position for formatting in place after reserve(); advance()
        /// commits the bytes written there.
        [[nodiscard]] inline auto cursor() noexcept -> char* { return data_ + size_; }
        inline void advance(std::size_t count) noexcept { size_ += count; }

        [[nodiscard]] inline auto take() && -> std::string
        {
            finish();
            return std::move(owned_);
        }

        inline void finish()
        {
            output_->resize(size_);
        }

    private:
        // Growth is rare; keep it out of the inlined fast paths.
        void grow(std::size_t extra)
        {
            resize_uninitialized(std::max(capacity_ * 2U, size_ + extra));
        }

        // Only bytes below size_ are ever read, so the spare capacity need
        // not be zero-filled first.
        void resize_uninitialized(std::size_t capacity)
        {
            output_->resize_and_overwrite(capacity,
                [](char*, std::size_t count) noexcept { return count; });
            data_ = output_->data();
            capacity_ = output_->size();
        }

        std::string owned_;
        std::string* output_ = &owned_;
        char* data_{};
        std::size_t size_{};
        std::size_t capacity_{};
    };

    // Direct serialization primitives. Their output is byte-identical to the
    // document backend's compact dump: object keys in std::map order, quote,
    // backslash, the short escapes for b f n r t and u00xx for the other controls, UTF-8
    // passed through and rejected when malformed.
    /// Appends `value` as a JSON string; false when it is not valid UTF-8.
    [[nodiscard]] auto append_string(json_writer& out, std::string_view value) -> bool;
    [[nodiscard]] auto append_double(json_writer& out, double value)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto append_document(json_writer& out, const document& value)
        -> std::expected<void, std::error_code>;
    /// Copies already-valid JSON text, dropping whitespace outside strings.
    void append_raw_json(json_writer& out, std::string_view text);
} // namespace detail

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

        [[nodiscard]] auto value() const -> reference
        {
            return operator*();
        }

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

    [[nodiscard]] static auto object() -> document
    {
        return document(object_type{});
    }

    [[nodiscard]] static auto object(
        std::initializer_list<std::pair<const char*, document>> values) -> document
    {
        return document(values);
    }

    [[nodiscard]] static auto array() -> document
    {
        return document(array_type{});
    }

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

    [[nodiscard]] auto is_discarded() const noexcept -> bool
    {
        return discarded_;
    }

    [[nodiscard]] auto is_null() const noexcept -> bool
    {
        return holds<std::nullptr_t>();
    }

    [[nodiscard]] auto is_boolean() const noexcept -> bool
    {
        return holds<bool>();
    }

    [[nodiscard]] auto is_string() const noexcept -> bool
    {
        return holds<std::string>();
    }

    [[nodiscard]] auto is_array() const noexcept -> bool
    {
        return holds<array_type>();
    }

    [[nodiscard]] auto is_object() const noexcept -> bool
    {
        return holds<object_type>();
    }

    [[nodiscard]] auto is_number_unsigned() const noexcept -> bool
    {
        return holds<std::uint64_t>();
    }

    [[nodiscard]] auto is_uint64() const noexcept -> bool
    {
        return is_number_unsigned();
    }

    [[nodiscard]] auto is_int64() const noexcept -> bool
    {
        return holds<std::int64_t>();
    }

    [[nodiscard]] auto is_double() const noexcept -> bool
    {
        return holds<double>();
    }

    [[nodiscard]] auto is_number_integer() const noexcept -> bool
    {
        return holds<std::uint64_t>() || holds<std::int64_t>();
    }

    [[nodiscard]] auto is_number_float() const noexcept -> bool
    {
        return holds<double>();
    }

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
        if (const auto* value = std::get_if<array_type>(&storage_))
            return value->empty();
        if (const auto* value = std::get_if<object_type>(&storage_))
            return value->empty();
        if (const auto* value = std::get_if<std::string>(&storage_))
            return value->empty();
        return is_null();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        if (const auto* value = std::get_if<array_type>(&storage_))
            return value->size();
        if (const auto* value = std::get_if<object_type>(&storage_))
            return value->size();
        if (const auto* value = std::get_if<std::string>(&storage_))
            return value->size();
        return 0;
    }

    [[nodiscard]] auto contains(std::string_view key) const -> bool
    {
        const auto* object = std::get_if<object_type>(&storage_);
        return object && object->contains(key);
    }

    auto operator[](std::string_view key) -> document&
    {
        if (is_null())
            storage_ = object_type{};
        return std::get<object_type>(storage_)[std::string{key}];
    }

    [[nodiscard]] auto operator[](std::string_view key) const -> const document&
    {
        return std::get<object_type>(storage_).at(std::string{key});
    }

    auto operator[](std::size_t index) -> document&
    {
        return std::get<array_type>(storage_)[index];
    }

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
        if (is_null())
            storage_ = array_type{};
        std::get<array_type>(storage_).push_back(std::move(value));
    }

    void pop_back()
    {
        std::get<array_type>(storage_).pop_back();
    }

    auto emplace(std::string key, document value) -> std::pair<object_type::iterator, bool>
    {
        if (is_null())
            storage_ = object_type{};
        return std::get<object_type>(storage_).emplace(std::move(key), std::move(value));
    }

    void clear()
    {
        if (auto* value = std::get_if<array_type>(&storage_))
            value->clear();
        else if (auto* value = std::get_if<object_type>(&storage_))
            value->clear();
        else if (auto* value = std::get_if<std::string>(&storage_))
            value->clear();
        else
            storage_ = nullptr;
    }

    [[nodiscard]] auto begin() -> iterator
    {
        if (auto* object = std::get_if<object_type>(&storage_))
            return iterator{this, object->begin()};
        if (auto* array = std::get_if<array_type>(&storage_))
            return iterator{this, array->begin()};
        return {};
    }

    [[nodiscard]] auto end() -> iterator
    {
        if (auto* object = std::get_if<object_type>(&storage_))
            return iterator{this, object->end()};
        if (auto* array = std::get_if<array_type>(&storage_))
            return iterator{this, array->end(), size()};
        return {};
    }

    [[nodiscard]] auto begin() const -> const_iterator
    {
        if (const auto* object = std::get_if<object_type>(&storage_))
            return const_iterator{this, object->begin()};
        if (const auto* array = std::get_if<array_type>(&storage_))
            return const_iterator{this, array->begin()};
        return {};
    }

    [[nodiscard]] auto end() const -> const_iterator
    {
        if (const auto* object = std::get_if<object_type>(&storage_))
            return const_iterator{this, object->end()};
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
        if constexpr (std::same_as<T, document>)
            return *this;
        else if constexpr (std::same_as<T, std::string>)
            return std::get<std::string>(storage_);
        else if constexpr (std::same_as<T, bool>)
            return std::get<bool>(storage_);
        else if constexpr (std::integral<T> && std::is_unsigned_v<T>)
        {
            if (const auto* value = std::get_if<std::uint64_t>(&storage_))
                return static_cast<T>(*value);
            if (const auto* value = std::get_if<std::int64_t>(&storage_))
                return static_cast<T>(*value);
            return static_cast<T>(std::get<double>(storage_));
        }
        else if constexpr (std::integral<T>)
        {
            if (const auto* value = std::get_if<std::int64_t>(&storage_))
                return static_cast<T>(*value);
            if (const auto* value = std::get_if<std::uint64_t>(&storage_))
                return static_cast<T>(*value);
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
            if (const auto* value = std::get_if<double>(&storage_))
                return static_cast<T>(*value);
            if (const auto* value = std::get_if<std::int64_t>(&storage_))
                return static_cast<T>(*value);
            return static_cast<T>(std::get<std::uint64_t>(storage_));
        }
        else
            static_assert(std::is_void_v<T>, "unsupported JSON conversion");
    }

    template <typename T>
    [[nodiscard]] auto as() const -> T
    {
        return get<T>();
    }

    [[nodiscard]] auto get_array() -> array_type&
    {
        return std::get<array_type>(storage_);
    }

    [[nodiscard]] auto get_array() const -> const array_type&
    {
        return std::get<array_type>(storage_);
    }

    [[nodiscard]] auto get_object() -> object_type&
    {
        return std::get<object_type>(storage_);
    }

    [[nodiscard]] auto get_object() const -> const object_type&
    {
        return std::get<object_type>(storage_);
    }

    template <typename T>
    [[nodiscard]] auto get_ref() -> T
    {
        using value_type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<value_type, std::string>)
            return std::get<std::string>(storage_);
        else if constexpr (std::same_as<value_type, array_type>)
            return std::get<array_type>(storage_);
        else
            return std::get<object_type>(storage_);
    }

    template <typename T>
    [[nodiscard]] auto get_ref() const -> T
    {
        using value_type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<value_type, std::string>)
            return std::get<std::string>(storage_);
        else if constexpr (std::same_as<value_type, array_type>)
            return std::get<array_type>(storage_);
        else
            return std::get<object_type>(storage_);
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
            return signed_number >= 0 && unsigned_value.get<std::uint64_t>() == static_cast<std::uint64_t>(signed_number);
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

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !is_null();
    }

private:
    template <typename T>
    [[nodiscard]] auto holds() const noexcept -> bool
    {
        return std::holds_alternative<T>(storage_);
    }

    storage_type storage_ = nullptr;
    bool discarded_ = false;
};

/**
 * @brief Non-owning view of JSON text that is already known to be valid.
 *
 * Use it to embed a value produced by a trusted encoder (for example a
 * PostgreSQL json/jsonb column) without building a document. The writer
 * copies the text once, dropping insignificant whitespace, so output stays
 * compact. The caller guarantees validity; the text is not checked when
 * written directly. It is parsed when converted to a document.
 */
struct raw_json
{
    std::string_view text;
};

using value = document;
using raw_value = document;
using array_type = document::array_type;
using object_type = document::object_type;

[[nodiscard]] inline auto object() -> document
{
    return document::object();
}

[[nodiscard]] inline auto object(
    std::initializer_list<std::pair<const char*, document>> values) -> document
{
    return document::object(values);
}

[[nodiscard]] inline auto array() -> document
{
    return document::array();
}

[[nodiscard]] inline auto array(
    std::initializer_list<document> values) -> document
{
    return document::array(values);
}

[[nodiscard]] inline auto find(document& source, std::string_view key)
    -> document*
{
    const auto found = source.find(key);
    return found == source.end() ? nullptr : std::addressof(*found);
}

[[nodiscard]] inline auto find(const document& source, std::string_view key)
    -> const document*
{
    const auto found = source.find(key);
    return found == source.end() ? nullptr : std::addressof(*found);
}

template <typename T>
[[nodiscard]] auto value_or(
    const document& source, std::string_view key, T fallback) -> T
{
    const auto* found = find(source, key);
    if (found == nullptr || found->is_null())
        return fallback;
    try
    {
        return found->template get<T>();
    }
    catch (...)
    {
        return fallback;
    }
}

[[nodiscard]] inline auto value_or(
    const document& source, std::string_view key, const char* fallback)
    -> std::string
{
    return value_or(source, key, std::string{fallback});
}

[[nodiscard]] inline auto value_or(
    const document& source, std::string_view key, std::string_view fallback)
    -> std::string
{
    return value_or(source, key, std::string{fallback});
}

[[nodiscard]] inline auto equivalent(
    const document& left, const document& right) -> bool
{
    return left == right;
}

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

namespace detail {
    template <typename T>
    struct document_codec;
}

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
concept BackendDocumentMapped = requires(
    const T& value, const document& source, bool option) {
    { detail::document_codec<T>::encode(value, option) }
    -> std::same_as<std::expected<document, std::error_code>>;
    { detail::document_codec<T>::decode(source, option) }
    -> std::same_as<std::expected<T, std::error_code>>;
};

template <typename T>
concept DocumentMapped = FieldDocumentMapped<T> || CustomDocumentMapped<T> ||
    BackendDocumentMapped<T>;

namespace detail {
    template <typename T> struct is_optional : std::false_type
    {
    };

    template <typename T> struct is_optional<std::optional<T>> : std::true_type
    {
        using value_type = T;
    };

    template <typename T> inline constexpr bool is_optional_v = is_optional<std::remove_cvref_t<T>>::value;
    template <typename T>
    concept StringLike = std::convertible_to<T, std::string_view>;
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
    [[nodiscard]] auto decode_value(const document& source, bool reject_unknown,
        bool preserve_defaults = false)
        -> std::expected<T, std::error_code>;

    template <typename T>
    [[nodiscard]] auto encode_object(const T& value, bool emit_nulls)
        -> std::expected<document, std::error_code>
    {
        auto result = document::object();
        auto status = std::expected<void, std::error_code>{};
        std::apply([&](const auto&... member)
            {
                ([&]
                    {
                        if (!status)
                            return;
                        const auto& field_value = value.*(member.member);
                        if constexpr (is_optional_v<decltype(field_value)>)
                            if (!field_value && !emit_nulls)
                                return;
                        auto encoded = encode_value(field_value, emit_nulls);
                        if (!encoded)
                            status = std::unexpected(encoded.error());
                        else
                            result[member.name] = std::move(*encoded);
                    }(),
                    ...);
            },
            document_traits<T>::fields());
        if (!status)
            return std::unexpected(status.error());
        return result;
    }

    template <typename T>
    [[nodiscard]] auto decode_object(const document& source, bool reject_unknown,
        bool preserve_defaults)
        -> std::expected<T, std::error_code>
    {
        if (!source.is_object())
            return std::unexpected(make_error_code(errc::type_mismatch));
        T result{};
        std::size_t known = 0;
        auto status = std::expected<void, std::error_code>{};
        std::apply([&](const auto&... member)
            {
                ([&]
                    {
                        if (!status)
                            return;
                        const auto found = source.find(member.name);
                        using member_type = std::remove_cvref_t<decltype(result.*(member.member))>;
                        if (found == source.end())
                        {
                            if (!preserve_defaults)
                            {
                                if constexpr (!is_optional_v<member_type>)
                                    status = std::unexpected(
                                        make_error_code(errc::missing_field));
                            }
                            return;
                        }
                        ++known;
                        auto decoded = decode_value<member_type>(
                            *found, reject_unknown, preserve_defaults);
                        if (!decoded)
                            status = std::unexpected(decoded.error());
                        else
                            result.*(member.member) = std::move(*decoded);
                    }(),
                    ...);
            },
            document_traits<T>::fields());
        if (!status)
            return std::unexpected(status.error());
        if (reject_unknown && known != source.size())
            return std::unexpected(make_error_code(errc::unknown_field));
        return result;
    }

    template <typename T>
    [[nodiscard]] auto encode_value(const T& value, bool emit_nulls)
        -> std::expected<document, std::error_code>
    {
        using value_type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<value_type, document>)
            return value;
        else if constexpr (std::same_as<value_type, raw_json>)
            return parse_document(value.text);
        else if constexpr (is_optional_v<value_type>)
            return value ? encode_value(*value, emit_nulls)
                         : std::expected<document, std::error_code>{document(nullptr)};
        else if constexpr (std::same_as<value_type, bool> || std::integral<value_type> ||
            std::floating_point<value_type> || StringLike<value_type>)
            return document(value);
        else if constexpr (std::same_as<value_type, std::filesystem::path>)
            return document(value.string());
        else if constexpr (std::is_enum_v<value_type>)
            return document(static_cast<std::underlying_type_t<value_type>>(value));
        else if constexpr (BackendDocumentMapped<value_type>)
            return document_codec<value_type>::encode(value, emit_nulls);
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
                if (!encoded)
                    return std::unexpected(encoded.error());
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
                if (!encoded)
                    return std::unexpected(encoded.error());
                result.push_back(std::move(*encoded));
            }
            return result;
        }
        else
            return std::unexpected(make_error_code(errc::type_mismatch));
    }

    template <typename T>
    [[nodiscard]] auto decode_value(const document& source, bool reject_unknown,
        bool preserve_defaults)
        -> std::expected<T, std::error_code>
    {
        using value_type = std::remove_cvref_t<T>;
        try
        {
            if constexpr (std::same_as<value_type, document>)
                return source;
            else if constexpr (is_optional_v<value_type>)
            {
                if (source.is_null())
                    return value_type{};
                auto decoded = decode_value<typename is_optional<value_type>::value_type>(
                    source, reject_unknown, preserve_defaults);
                if (!decoded)
                    return std::unexpected(decoded.error());
                return value_type{std::move(*decoded)};
            }
            else if constexpr (std::same_as<value_type, std::string>)
            {
                if (!source.is_string())
                    return std::unexpected(make_error_code(errc::type_mismatch));
                return source.template get<std::string>();
            }
            else if constexpr (std::same_as<value_type, std::filesystem::path>)
            {
                if (!source.is_string())
                    return std::unexpected(make_error_code(errc::type_mismatch));
                return std::filesystem::path{source.template get<std::string>()};
            }
            else if constexpr (std::same_as<value_type, bool>)
            {
                if (!source.is_boolean())
                    return std::unexpected(make_error_code(errc::type_mismatch));
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
                        if (value >= 0 && static_cast<std::uint64_t>(value) <= std::numeric_limits<value_type>::max())
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
                auto decoded = decode_value<std::underlying_type_t<value_type>>(
                    source, reject_unknown, preserve_defaults);
                if (!decoded)
                    return std::unexpected(decoded.error());
                return static_cast<value_type>(*decoded);
            }
            else if constexpr (BackendDocumentMapped<value_type>)
                return document_codec<value_type>::decode(source, reject_unknown);
            else if constexpr (CustomDocumentMapped<value_type>)
                return document_traits<value_type>::decode(source, reject_unknown);
            else if constexpr (FieldDocumentMapped<value_type>)
                return decode_object<value_type>(
                    source, reject_unknown, preserve_defaults);
            else if constexpr (AssociativeObject<value_type>)
            {
                if (!source.is_object())
                    return std::unexpected(make_error_code(errc::type_mismatch));
                value_type result;
                for (auto iterator = source.begin(); iterator != source.end(); ++iterator)
                {
                    auto decoded = decode_value<typename value_type::mapped_type>(
                        iterator.value(), reject_unknown, preserve_defaults);
                    if (!decoded)
                        return std::unexpected(decoded.error());
                    result.emplace(iterator.key(), std::move(*decoded));
                }
                return result;
            }
            else if constexpr (Sequence<value_type>)
            {
                if (!source.is_array())
                    return std::unexpected(make_error_code(errc::type_mismatch));
                value_type result;
                for (const auto& entry : source)
                {
                    auto decoded = decode_value<typename value_type::value_type>(
                        entry, reject_unknown, preserve_defaults);
                    if (!decoded)
                        return std::unexpected(decoded.error());
                    result.push_back(std::move(*decoded));
                }
                return result;
            }
            else
                return std::unexpected(make_error_code(errc::type_mismatch));
        }
        catch (...)
        {
            return std::unexpected(make_error_code(errc::type_mismatch));
        }
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

/**
 * @brief Converts a document tree while retaining C++ defaults for omitted fields.
 *
 * Unknown fields remain errors. The policy is propagated through nested mapped
 * objects and sequence elements, which makes it suitable for typed options.
 */
template <typename T>
[[nodiscard]] auto from_document_with_defaults(const document& source)
    -> std::expected<T, std::error_code>
{
    return detail::decode_value<T>(source, true, true);
}

namespace detail {
    template <typename T> struct is_std_map : std::false_type
    {
    };

    // std::map<std::string, V> with the standard ordering: iterated in the
    // same key order as the backend's own object storage.
    template <typename V, typename A>
    struct is_std_map<std::map<std::string, V, std::less<>, A>> : std::true_type
    {
    };

    template <typename V, typename A>
    struct is_std_map<std::map<std::string, V, std::less<std::string>, A>> : std::true_type
    {
    };

    template <typename T>
    [[nodiscard]] auto write_value(json_writer& out, const T& value, bool emit_nulls)
        -> std::expected<void, std::error_code>;

    template <std::integral T>
    void append_integer(json_writer& out, T value)
    {
        // Format straight into reserved output; 20 digits plus a sign.
        constexpr std::size_t widest = 21U;
        out.reserve(widest);
        auto* const first = out.cursor();
        const auto [end, error] = std::to_chars(first, first + widest, value);
        out.advance(static_cast<std::size_t>(end - first));
    }

    /// The order sorted_field_order() describes, computed at compile time:
    /// position P holds the field index to emit there, or the field count for
    /// a position dropped because a later field repeats its name.
    template <typename T>
    [[nodiscard]] consteval auto compile_time_field_order()
    {
        constexpr auto fields = document_traits<T>::fields();
        constexpr std::size_t count = std::tuple_size_v<std::remove_cvref_t<decltype(fields)>>;
        std::array<std::string_view, count> names{};
        [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            ((names[I] = std::get<I>(fields).name), ...);
        }(std::make_index_sequence<count>{});
        std::array<std::size_t, count> indices{};
        for (std::size_t index = 0; index < count; ++index)
            indices[index] = index;
        // Stable insertion sort: equal names keep declaration order.
        for (std::size_t position = 1; position < count; ++position)
            for (std::size_t at = position; at > 0 && names[indices[at]] < names[indices[at - 1U]]; --at)
                std::swap(indices[at], indices[at - 1U]);
        std::array<std::size_t, count> order{};
        for (std::size_t position = 0; position < count; ++position)
            order[position] = position + 1U < count &&
                    names[indices[position + 1U]] == names[indices[position]]
                ? count
                : indices[position];
        return order;
    }

    /// Field indices of T in the order their names sort, which is the order
    /// the document backend's std::map emits them. A repeated name keeps only
    /// its last field, as assignment into that map would.
    template <typename T>
    [[nodiscard]] auto sorted_field_order() -> const auto&
    {
        static constexpr std::size_t count = std::tuple_size_v<
            std::remove_cvref_t<decltype(document_traits<T>::fields())>>;
        static const auto order = []
        {
            const auto fields = document_traits<T>::fields();
            std::array<std::string_view, count> names{};
            [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                ((names[I] = std::get<I>(fields).name), ...);
            }(std::make_index_sequence<count>{});
            std::array<std::size_t, count> indices{};
            for (std::size_t index = 0; index < count; ++index)
                indices[index] = index;
            std::ranges::stable_sort(indices, [&](std::size_t lhs, std::size_t rhs)
                { return names[lhs] < names[rhs]; });
            std::vector<std::size_t> unique;
            for (std::size_t position = 0; position < count; ++position)
            {
                const auto index = indices[position];
                // Equal names are adjacent after the stable sort; the last
                // declared field among them wins.
                if (position + 1U < count && names[indices[position + 1U]] == names[index])
                    continue;
                unique.push_back(index);
            }
            return unique;
        }();
        return order;
    }

    template <typename T>
    [[nodiscard]] auto write_object(json_writer& out, const T& value, bool emit_nulls)
        -> std::expected<void, std::error_code>
    {
        using fields_type = std::remove_cvref_t<decltype(document_traits<T>::fields())>;
        static constexpr std::size_t count = std::tuple_size_v<fields_type>;
        static const fields_type fields = document_traits<T>::fields();
        // Each member's `"name":` prefix, escaped once per type. An empty
        // entry means the name itself is not valid UTF-8.
        static const auto keys = []<std::size_t... I>(std::index_sequence<I...>)
        {
            std::array<std::string, count> result;
            ([&]
                {
                    json_writer encoded{std::get<I>(fields).name.size() + 3U};
                    if (append_string(encoded, std::get<I>(fields).name))
                    {
                        encoded.put(':');
                        result[I] = std::move(encoded).take();
                    }
                }(),
                ...);
            return result;
        }(std::make_index_sequence<count>{});
        // The emission order is a compile-time constant, so every member is
        // written by a direct, inlinable call instead of through a table of
        // function pointers walked at run time.
        static constexpr auto order = compile_time_field_order<T>();
        const auto write_member = [&]<std::size_t I>(bool& first)
            -> std::expected<void, std::error_code>
        {
            const auto& member = std::get<I>(fields);
            const auto& field_value = value.*(member.member);
            if constexpr (is_optional_v<std::remove_cvref_t<decltype(field_value)>>)
                if (!field_value && !emit_nulls)
                    return {};
            const auto& key = keys[I];
            if (key.empty())
                return std::unexpected(make_error_code(errc::serialization_failed));
            out.reserve(key.size() + 1U);
            if (!std::exchange(first, false))
                out.put_unchecked(',');
            out.append_unchecked(key.data(), key.size());
            return write_value(out, field_value, emit_nulls);
        };
        out.put('{');
        bool first = true;
        std::expected<void, std::error_code> result;
        [&]<std::size_t... P>(std::index_sequence<P...>)
        {
            // Stops at the first failure; skipped positions hold `count`.
            (void)((order[P] == count ||
                       (result = write_member.template operator()<(order[P] < count ? order[P] : 0U)>(first))) &&
                ...);
        }(std::make_index_sequence<count>{});
        if (!result)
            return result;
        out.put('}');
        return {};
    }

    template <typename T>
    [[nodiscard]] auto write_value(json_writer& out, const T& value, bool emit_nulls)
        -> std::expected<void, std::error_code>
    {
        using value_type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<value_type, document>)
            return append_document(out, value);
        else if constexpr (std::same_as<value_type, raw_json>)
        {
            append_raw_json(out, value.text);
            return {};
        }
        else if constexpr (is_optional_v<value_type>)
        {
            if (!value)
            {
                out.append("null");
                return {};
            }
            return write_value(out, *value, emit_nulls);
        }
        else if constexpr (std::same_as<value_type, bool>)
        {
            out.append(value ? "true" : "false");
            return {};
        }
        else if constexpr (std::integral<value_type>)
        {
            // The document backend stores every integer as int64 or uint64.
            if constexpr (std::is_unsigned_v<value_type>)
                append_integer(out, static_cast<std::uint64_t>(value));
            else
                append_integer(out, static_cast<std::int64_t>(value));
            return {};
        }
        else if constexpr (std::floating_point<value_type>)
            return append_double(out, static_cast<double>(value));
        else if constexpr (StringLike<value_type>)
        {
            if (!append_string(out, std::string_view{value}))
                return std::unexpected(make_error_code(errc::serialization_failed));
            return {};
        }
        else if constexpr (std::same_as<value_type, std::filesystem::path>)
        {
            if (!append_string(out, value.string()))
                return std::unexpected(make_error_code(errc::serialization_failed));
            return {};
        }
        else if constexpr (std::is_enum_v<value_type>)
        {
            append_integer(out, static_cast<std::underlying_type_t<value_type>>(value));
            return {};
        }
        else if constexpr (BackendDocumentMapped<value_type> ||
            CustomDocumentMapped<value_type>)
        {
            auto encoded = encode_value(value, emit_nulls);
            if (!encoded)
                return std::unexpected(encoded.error());
            return append_document(out, *encoded);
        }
        else if constexpr (FieldDocumentMapped<value_type>)
            return write_object(out, value, emit_nulls);
        else if constexpr (is_std_map<value_type>::value)
        {
            out.put('{');
            bool first = true;
            for (const auto& [key, entry] : value)
            {
                if (!std::exchange(first, false))
                    out.put(',');
                if (!append_string(out, std::string_view{key}))
                    return std::unexpected(make_error_code(errc::serialization_failed));
                out.put(':');
                if (auto written = write_value(out, entry, emit_nulls); !written)
                    return written;
            }
            out.put('}');
            return {};
        }
        else if constexpr (AssociativeObject<value_type>)
        {
            // Other maps have no key order matching the backend's; keep its
            // ordering by going through the document.
            auto encoded = encode_value(value, emit_nulls);
            if (!encoded)
                return std::unexpected(encoded.error());
            return append_document(out, *encoded);
        }
        else if constexpr (Sequence<value_type>)
        {
            out.put('[');
            bool first = true;
            for (const auto& entry : value)
            {
                if (!std::exchange(first, false))
                    out.put(',');
                if (auto written = write_value(out, entry, emit_nulls); !written)
                    return written;
            }
            out.put(']');
            return {};
        }
        else
            return std::unexpected(make_error_code(errc::type_mismatch));
    }

    template <typename T>
    [[nodiscard]] auto write_direct(const T& value, bool emit_nulls)
        -> std::expected<std::string, std::error_code>
    {
        // Successive documents of one type are usually of similar size, so
        // start from the last size this thread produced instead of growing
        // from a small buffer through several reallocations.
        thread_local std::size_t last_size = 512U;
        json_writer out{std::max<std::size_t>(512U, last_size + last_size / 8U)};
        if (auto written = write_value(out, value, emit_nulls); !written)
            return std::unexpected(written.error());
        last_size = out.size();
        return std::move(out).take();
    }

    template <typename T>
    [[nodiscard]] auto write_direct_to(
        std::string& output, const T& value, bool emit_nulls)
        -> std::expected<void, std::error_code>
    {
        thread_local std::size_t last_size = 512U;
        json_writer out{
            output, std::max<std::size_t>(512U, last_size + last_size / 8U)};
        if (auto written = write_value(out, value, emit_nulls); !written)
        {
            output.clear();
            return std::unexpected(written.error());
        }
        last_size = out.size();
        out.finish();
        return {};
    }
} // namespace detail

struct default_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input) -> std::expected<T, std::error_code>
    {
        auto parsed = parse_document(input);
        if (!parsed)
            return std::unexpected(parsed.error());
        return detail::decode_value<T>(*parsed, true);
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value) -> std::expected<std::string, std::error_code>
    {
        return detail::write_direct(value, false);
    }
};

struct lenient_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input) -> std::expected<T, std::error_code>
    {
        auto parsed = parse_document(input);
        if (!parsed)
            return std::unexpected(parsed.error());
        return detail::decode_value<T>(*parsed, false);
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value) -> std::expected<std::string, std::error_code>
    {
        return default_codec::encode(value);
    }
};

/**
 * @brief Strict-key codec that retains C++ defaults for omitted fields.
 */
struct defaulted_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        auto parsed = parse_document(input);
        if (!parsed)
            return std::unexpected(parsed.error());
        return from_document_with_defaults<T>(*parsed);
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
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
        return detail::write_direct(value, true);
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

/**
 * @brief Serializes directly into caller-owned storage.
 *
 * Existing capacity is reused. This is the preferred path for reusable HTTP
 * response builders and other connection-local output buffers.
 */
template <typename T>
[[nodiscard]] auto write_to(std::string& output, const T& value)
    -> std::expected<void, std::error_code>
{
    return detail::write_direct_to(output, value, false);
}

template <typename T>
[[nodiscard]] auto parse_allow_unknown(std::string_view input)
    -> std::expected<T, std::error_code>
{
    return lenient_codec::template decode<T>(input);
}

template <typename T>
[[nodiscard]] auto parse_lenient(std::string_view input)
    -> std::expected<T, std::error_code>
{
    return parse_allow_unknown<T>(input);
}

template <typename T>
[[nodiscard]] auto write_explicit_nulls(const T& value)
    -> std::expected<std::string, std::error_code>
{
    return explicit_null_codec::template encode<T>(value);
}

} // namespace cnetmod::json

export template <>
struct std::is_error_code_enum<cnetmod::json::errc> : std::true_type
{
};
