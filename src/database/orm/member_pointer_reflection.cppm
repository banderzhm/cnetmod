export module cnetmod.orm.member_pointer_reflection;

import std;
import cnetmod.orm.model_metadata;

export namespace cnetmod::orm {

// =============================================================================
// Member pointer to column name resolution
// =============================================================================

/// @brief Resolve database column name from a member pointer
/// @tparam T Model type
/// @tparam U Member type
/// @param member_ptr Pointer to member of T
/// @return Database column name corresponding to the member
/// @throws std::runtime_error if member pointer does not match any registered field
template <Model T, typename U>
[[nodiscard]] auto resolve_column_name(U T::* member_ptr) -> std::string_view
{
    const auto& meta = model_traits<T>::meta();
    for (const auto& field : meta.fields)
    {
        if (field.matches(member_ptr))
        {
            return field.col.column_name;
        }
    }

    // If no match found, throw an error with helpful message
    throw std::runtime_error(
        std::format("Member pointer does not match any registered field in "
                    "model '{}'. Make sure the field is declared with "
                    "CNETMOD_FIELD macro.",
            meta.table_name));
}

} // namespace cnetmod::orm
