export module cnetmod.utils.concurrent_containers.copy_on_write_value;

import std;

namespace cnetmod::concurrent_containers {

/// Immutable-snapshot container. Readers never lock and writers publish a
/// complete replacement with release/acquire shared_ptr operations.
export template <class T>
class copy_on_write_value
{
public:
    copy_on_write_value();
    explicit copy_on_write_value(T initial);

    [[nodiscard]] auto read() const noexcept -> std::shared_ptr<const T>;
    [[nodiscard]] auto snapshot() const -> T;
    void store(T value);

    template <class Mutator>
    requires std::invocable<Mutator&, T&>
    auto update(Mutator&& mutator) -> void;

    template <class Predicate>
    requires std::predicate<Predicate&, const T&>
    [[nodiscard]] auto compare_update(Predicate&& predicate, T replacement) -> bool;

private:
    std::shared_ptr<const T> value_;
};

/// Backward-compatible generic spelling. New code should state the snapshot
/// domain explicitly with copy_on_write_value/vector/map/set.
export template <class T>
using copy_on_write = copy_on_write_value<T>;

} // namespace cnetmod::concurrent_containers

#include "copy_on_write_value.inl"
