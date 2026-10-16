extern "C" auto mi_version() -> int;
extern "C" auto mi_is_in_heap_region(const void* pointer) noexcept -> bool;

auto main() -> int
{
    if (mi_version() < 305)
        return 1;

    auto* value = new unsigned long long{42};
    const auto owned = mi_is_in_heap_region(value);
    delete value;
    return owned ? 0 : 2;
}
