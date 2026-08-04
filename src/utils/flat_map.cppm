module;

#include <cnetmod/config.hpp>

export module cnetmod.utils.flat_map;

import std;

namespace cnetmod {

/// A cache-friendly ordered associative container backed by contiguous
/// storage.  This project-owned interface keeps code portable when the active
/// C++ standard library does not yet provide std::flat_map.
export template <class Key, class Mapped, class Compare = std::less<Key>,
    class Allocator = std::allocator<std::pair<Key, Mapped>>>
class flat_map
{
public:
    using key_type = Key;
    using mapped_type = Mapped;
    using value_type = std::pair<Key, Mapped>;
    using key_compare = Compare;
    using allocator_type = Allocator;
    using storage_type = std::vector<value_type, Allocator>;
    using size_type = typename storage_type::size_type;
    using difference_type = typename storage_type::difference_type;
    using reference = typename storage_type::reference;
    using const_reference = typename storage_type::const_reference;
    using iterator = typename storage_type::iterator;
    using const_iterator = typename storage_type::const_iterator;
    using reverse_iterator = typename storage_type::reverse_iterator;
    using const_reverse_iterator = typename storage_type::const_reverse_iterator;

    flat_map() = default;

    explicit flat_map(const Compare& compare,
        const Allocator& allocator = Allocator{})
        : values_(allocator), compare_(compare)
    {
    }

    explicit flat_map(const Allocator& allocator)
        : values_(allocator)
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    flat_map(Iterator first, Sentinel last, const Compare& compare = Compare{},
        const Allocator& allocator = Allocator{})
        : values_(allocator), compare_(compare)
    {
        insert(first, last);
    }

    flat_map(std::initializer_list<value_type> values,
        const Compare& compare = Compare{},
        const Allocator& allocator = Allocator{})
        : flat_map(values.begin(), values.end(), compare, allocator)
    {
    }

    [[nodiscard]] auto begin() noexcept -> iterator
    {
        return values_.begin();
    }

    [[nodiscard]] auto begin() const noexcept -> const_iterator
    {
        return values_.begin();
    }

    [[nodiscard]] auto cbegin() const noexcept -> const_iterator
    {
        return values_.cbegin();
    }

    [[nodiscard]] auto end() noexcept -> iterator
    {
        return values_.end();
    }

    [[nodiscard]] auto end() const noexcept -> const_iterator
    {
        return values_.end();
    }

    [[nodiscard]] auto cend() const noexcept -> const_iterator
    {
        return values_.cend();
    }

    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator
    {
        return values_.rbegin();
    }

    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator
    {
        return values_.rbegin();
    }

    [[nodiscard]] auto rend() noexcept -> reverse_iterator
    {
        return values_.rend();
    }

    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator
    {
        return values_.rend();
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return values_.empty();
    }

    [[nodiscard]] auto size() const noexcept -> size_type
    {
        return values_.size();
    }

    [[nodiscard]] auto capacity() const noexcept -> size_type
    {
        return values_.capacity();
    }

    [[nodiscard]] auto max_size() const noexcept -> size_type
    {
        return values_.max_size();
    }

    void reserve(size_type capacity)
    {
        values_.reserve(capacity);
    }

    void shrink_to_fit()
    {
        values_.shrink_to_fit();
    }

    void clear() noexcept
    {
        values_.clear();
    }

    [[nodiscard]] auto key_comp() const -> key_compare
    {
        return compare_;
    }

    [[nodiscard]] auto get_allocator() const noexcept -> allocator_type
    {
        return values_.get_allocator();
    }

    [[nodiscard]] auto lower_bound(const key_type& key) -> iterator
    {
        return lower_bound_impl(key);
    }

    [[nodiscard]] auto lower_bound(const key_type& key) const -> const_iterator
    {
        return lower_bound_impl(key);
    }

    template <class Lookup>
    requires requires(const Compare& compare, const Key& stored, const Lookup& lookup) {
        { compare(stored, lookup) } -> std::convertible_to<bool>;
    }
    [[nodiscard]] auto lower_bound(const Lookup& key) -> iterator
    {
        return lower_bound_impl(key);
    }

    template <class Lookup>
    requires requires(const Compare& compare, const Key& stored, const Lookup& lookup) {
        { compare(stored, lookup) } -> std::convertible_to<bool>;
    }
    [[nodiscard]] auto lower_bound(const Lookup& key) const -> const_iterator
    {
        return lower_bound_impl(key);
    }

    [[nodiscard]] auto find(const key_type& key) -> iterator
    {
        return find_impl(key);
    }

    [[nodiscard]] auto find(const key_type& key) const -> const_iterator
    {
        return find_impl(key);
    }

    template <class Lookup>
    requires requires(const Compare& compare, const Key& stored, const Lookup& lookup) {
        { compare(stored, lookup) } -> std::convertible_to<bool>;
        { compare(lookup, stored) } -> std::convertible_to<bool>;
    }
    [[nodiscard]] auto find(const Lookup& key) -> iterator
    {
        return find_impl(key);
    }

    template <class Lookup>
    requires requires(const Compare& compare, const Key& stored, const Lookup& lookup) {
        { compare(stored, lookup) } -> std::convertible_to<bool>;
        { compare(lookup, stored) } -> std::convertible_to<bool>;
    }
    [[nodiscard]] auto find(const Lookup& key) const -> const_iterator
    {
        return find_impl(key);
    }

    [[nodiscard]] auto contains(const key_type& key) const -> bool
    {
        return find(key) != end();
    }

    template <class Lookup>
    requires requires(const Compare& compare, const Key& stored,
        const Lookup& lookup) {
        { compare(stored, lookup) } -> std::convertible_to<bool>;
        { compare(lookup, stored) } -> std::convertible_to<bool>;
    }
    [[nodiscard]] auto contains(const Lookup& key) const -> bool
    {
        return find(key) != end();
    }

    [[nodiscard]] auto count(const key_type& key) const -> size_type
    {
        return contains(key) ? 1U : 0U;
    }

    template <class Lookup>
    requires requires(const Compare& compare, const Key& stored,
        const Lookup& lookup) {
        { compare(stored, lookup) } -> std::convertible_to<bool>;
        { compare(lookup, stored) } -> std::convertible_to<bool>;
    }
    [[nodiscard]] auto count(const Lookup& key) const -> size_type
    {
        return contains(key) ? 1U : 0U;
    }

    auto at(const key_type& key) -> mapped_type&
    {
        const auto found = find(key);
        if (found == end())
            throw std::out_of_range("cnetmod::flat_map::at");
        return found->second;
    }

    auto at(const key_type& key) const -> const mapped_type&
    {
        const auto found = find(key);
        if (found == end())
            throw std::out_of_range("cnetmod::flat_map::at");
        return found->second;
    }

    auto operator[](const key_type& key) -> mapped_type&
    {
        return try_emplace(key).first->second;
    }

    auto operator[](key_type&& key) -> mapped_type&
    {
        return try_emplace(std::move(key)).first->second;
    }

    auto insert(const value_type& value) -> std::pair<iterator, bool>
    {
        return insert_value(value);
    }

    auto insert(value_type&& value) -> std::pair<iterator, bool>
    {
        return insert_value(std::move(value));
    }

    template <class Pair>
    requires std::constructible_from<value_type, Pair&&>
    auto insert(Pair&& value) -> std::pair<iterator, bool>
    {
        return insert_value(value_type(std::forward<Pair>(value)));
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    void insert(Iterator first, Sentinel last)
    {
        for (; first != last; ++first)
            insert(*first);
    }

    void insert(std::initializer_list<value_type> values)
    {
        insert(values.begin(), values.end());
    }

    template <class... Arguments>
    auto emplace(Arguments&&... arguments) -> std::pair<iterator, bool>
    {
        return insert_value(value_type(std::forward<Arguments>(arguments)...));
    }

    template <class KeyArgument, class... Arguments>
    auto try_emplace(KeyArgument&& key, Arguments&&... arguments)
        -> std::pair<iterator, bool>
    {
        auto position = lower_bound(key);
        if (position != end() && equivalent(position->first, key))
            return {position, false};
        position = values_.emplace(position,
            std::piecewise_construct,
            std::forward_as_tuple(std::forward<KeyArgument>(key)),
            std::forward_as_tuple(std::forward<Arguments>(arguments)...));
        return {position, true};
    }

    template <class KeyArgument, class ValueArgument>
    auto insert_or_assign(KeyArgument&& key, ValueArgument&& value)
        -> std::pair<iterator, bool>
    {
        auto position = lower_bound(key);
        if (position != end() && equivalent(position->first, key))
        {
            position->second = std::forward<ValueArgument>(value);
            return {position, false};
        }
        position = values_.emplace(position,
            std::forward<KeyArgument>(key), std::forward<ValueArgument>(value));
        return {position, true};
    }

    auto erase(const_iterator position) -> iterator
    {
        return values_.erase(position);
    }

    auto erase(const_iterator first, const_iterator last) -> iterator
    {
        return values_.erase(first, last);
    }

    auto erase(const key_type& key) -> size_type
    {
        const auto found = find(key);
        if (found == end())
            return 0;
        values_.erase(found);
        return 1;
    }

    template <class Lookup>
    requires requires(const Compare& compare, const Key& stored,
        const Lookup& lookup) {
        { compare(stored, lookup) } -> std::convertible_to<bool>;
        { compare(lookup, stored) } -> std::convertible_to<bool>;
    }
    auto erase(const Lookup& key) -> size_type
    {
        const auto found = find(key);
        if (found == end())
            return 0;
        values_.erase(found);
        return 1;
    }

    void swap(flat_map& other) noexcept(
        std::is_nothrow_swappable_v<storage_type> &&
        std::is_nothrow_swappable_v<Compare>)
    {
        using std::swap;
        swap(values_, other.values_);
        swap(compare_, other.compare_);
    }

private:
    template <class Lookup>
    [[nodiscard]] auto lower_bound_impl(const Lookup& key) -> iterator
    {
        return std::lower_bound(values_.begin(), values_.end(), key,
            [this](const value_type& value, const Lookup& lookup)
            {
                return compare_(value.first, lookup);
            });
    }

    template <class Lookup>
    [[nodiscard]] auto lower_bound_impl(const Lookup& key) const -> const_iterator
    {
        return std::lower_bound(values_.begin(), values_.end(), key,
            [this](const value_type& value, const Lookup& lookup)
            {
                return compare_(value.first, lookup);
            });
    }

    template <class Left, class Right>
    [[nodiscard]] auto equivalent(const Left& left, const Right& right) const -> bool
    {
        return !compare_(left, right) && !compare_(right, left);
    }

    template <class Lookup>
    [[nodiscard]] auto find_impl(const Lookup& key) -> iterator
    {
        const auto position = lower_bound_impl(key);
        return position != end() && equivalent(position->first, key)
            ? position
            : end();
    }

    template <class Lookup>
    [[nodiscard]] auto find_impl(const Lookup& key) const -> const_iterator
    {
        const auto position = lower_bound_impl(key);
        return position != end() && equivalent(position->first, key)
            ? position
            : end();
    }

    template <class Value>
    auto insert_value(Value&& value) -> std::pair<iterator, bool>
    {
        auto position = lower_bound(value.first);
        if (position != end() && equivalent(position->first, value.first))
            return {position, false};
        position = values_.insert(position, std::forward<Value>(value));
        return {position, true};
    }

    storage_type values_;
    [[no_unique_address]] Compare compare_{};
};

export template <class Key, class Mapped, class Compare, class Allocator>
void swap(flat_map<Key, Mapped, Compare, Allocator>& left,
    flat_map<Key, Mapped, Compare, Allocator>& right) noexcept(noexcept(left.swap(right)))
{
    left.swap(right);
}

} // namespace cnetmod
