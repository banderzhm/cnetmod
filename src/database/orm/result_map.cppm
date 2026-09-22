export module cnetmod.orm.result_map;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.model_metadata;
import cnetmod.orm.xml_mapper_parser;
import cnetmod.coro.task;
import cnetmod.coro.mutex;

namespace cnetmod::orm {

// =============================================================================
// result_mapping — Single column-to-property mapping
// =============================================================================

export struct result_mapping
{
    std::string property;     // Property name in model
    std::string column;       // Column name in result set
    std::string jdbc_type;    // JDBC type (optional)
    std::string type_handler; // Custom type handler (optional)
    bool is_id = false;       // Is this the ID field?
};

// =============================================================================
// association — One-to-one or many-to-one relationship
// =============================================================================

export struct association
{
    std::string property;   // Property name in model
    std::string column;     // Column name for join
    std::string select;     // Statement ID for nested select
    std::string result_map; // ResultMap ID for nested result
    std::string jdbc_type;
};

// =============================================================================
// collection — One-to-many relationship
// =============================================================================

export struct collection
{
    std::string property;   // Property name in model (collection)
    std::string column;     // Column name for join
    std::string select;     // Statement ID for nested select
    std::string result_map; // ResultMap ID for nested result
    std::string of_type;    // Element type of collection
};

// =============================================================================
// result_map_def — Complete ResultMap definition
// =============================================================================

export struct result_map_def
{
    std::string id;           // ResultMap ID
    std::string type;         // Target type (class name)
    bool auto_mapping = true; // Enable auto-mapping for unmapped columns

    std::vector<result_mapping> id_mappings;     // <id> mappings
    std::vector<result_mapping> result_mappings; // <result> mappings
    std::vector<association> associations;       // <association> mappings
    std::vector<collection> collections;         // <collection> mappings

    // Get all column mappings (id + result)
    auto all_mappings() const -> std::vector<result_mapping>;

    // Find mapping by property name
    auto find_by_property(std::string_view prop) const -> const result_mapping*;

    // Find mapping by column name
    auto find_by_column(std::string_view col) const -> const result_mapping*;
};

// Runtime object graph used by XML mappers. XML carries property names, not
// C++ member pointers, therefore this is the type-safe boundary between the
// dynamic mapper layer and application-specific DTOs.
export struct mapped_object
{
    std::unordered_map<std::string, param_value> values;
    std::unordered_map<std::string, mapped_object> associations;
    std::unordered_map<std::string, std::vector<mapped_object>> collections;
};

namespace detail {

    inline auto mapped_param_to_field_value(const param_value& value) -> field_value
    {
        using kind = param_value::kind_t;
        switch (value.kind)
        {
        case kind::null_kind:
            return field_value::null();
        case kind::int64_kind:
            return field_value::from_int64(value.int_val);
        case kind::uint64_kind:
            return field_value::from_uint64(value.uint_val);
        case kind::double_kind:
            return field_value::from_double(value.double_val);
        case kind::string_kind:
            return field_value::from_string(value.str_val);
        case kind::blob_kind:
            return field_value::from_blob(value.str_val);
        case kind::date_kind:
            return field_value::from_date(value.date_val);
        case kind::datetime_kind:
            return field_value::from_datetime(value.datetime_val);
        case kind::time_kind:
            return field_value::from_time(value.time_val);
        }
        return field_value::null();
    }

} // namespace detail

/// Extension point for the part of a resultMap that cannot be inferred from
/// XML: the concrete C++ member which receives an association or collection.
/// Keep this explicit instead of guessing member offsets from property strings.
/// A specialization can use mapped_association_as()/mapped_collection_as().
export template <ResultRecord T> struct xml_object_graph_binder
{
    static void bind(T&, const mapped_object&) {}
};

/// Type-safe final projection for XML resultMap output. The XML layer owns
/// property names and joins; the C++ model owns actual members through the
/// existing CNETMOD_MODEL setters. Both resultMap property names and declared
/// database column names are accepted, which keeps aliases explicit in XML.
/// xml_object_graph_binder<T> completes any explicitly-declared typed relations.
export template <ResultRecord T>
auto from_mapped_object(const mapped_object& source) -> T
{
    T result{};
    for (const auto& field : result_fields<T>())
    {
        const auto property = source.values.find(std::string(field.col.field_name));
        const auto value = property != source.values.end() ? property
                                                           : source.values.find(std::string(field.col.column_name));
        if (value != source.values.end() && field.setter)
            field.setter(result, detail::mapped_param_to_field_value(value->second));
    }
    xml_object_graph_binder<T>::bind(result, source);
    return result;
}

export template <ResultRecord T>
auto from_mapped_objects(const std::vector<mapped_object>& source)
    -> std::vector<T>
{
    std::vector<T> result;
    result.reserve(source.size());
    for (const auto& object : source)
        result.push_back(from_mapped_object<T>(object));
    return result;
}

/// Convert one named <association> to the application model, if the joined
/// result contained a child object. This deliberately returns optional so a
/// nullable SQL join is represented without a sentinel DTO.
export template <ResultRecord T>
auto mapped_association_as(const mapped_object& source,
    std::string_view property) -> std::optional<T>
{
    const auto found = source.associations.find(std::string(property));
    if (found == source.associations.end())
        return std::nullopt;
    return from_mapped_object<T>(found->second);
}

/// Convert one named <collection> to a vector of application models. Joined
/// row de-duplication has already happened in result_map_applier, so the
/// vector preserves the mapper's object-graph semantics.
export template <ResultRecord T>
auto mapped_collection_as(const mapped_object& source,
    std::string_view property) -> std::vector<T>
{
    const auto found = source.collections.find(std::string(property));
    if (found == source.collections.end())
        return {};
    return from_mapped_objects<T>(found->second);
}

// Explicit coroutine lazy loader. Unlike Java proxy interception this never
// blocks a property access; callers must co_await get().
export template <class T> class lazy_relation
{
public:
    using loader_type = std::function<task<std::expected<T, std::string>>()>;

    lazy_relation() : state_(std::make_shared<state>()) {}

    explicit lazy_relation(loader_type loader)
        : state_(std::make_shared<state>(std::move(loader))) {}

    auto get() -> task<std::expected<const T*, std::string>>
    {
        auto state = state_;
        if (state->value)
            co_return &*state->value;
        // This coroutine mutex establishes a single-flight boundary for one
        // relation only. It suspends competing coroutines rather than tying up
        // an I/O worker, then rechecks after the first loader completes.
        co_await state->mutex.lock();
        cnetmod::async_lock_guard guard{state->mutex, std::adopt_lock};
        if (state->value)
            co_return &*state->value;
        if (!state->loader)
            co_return std::unexpected("lazy relation has no loader");
        auto loaded = co_await state->loader();
        if (!loaded)
            co_return std::unexpected(loaded.error());
        state->value = std::move(*loaded);
        co_return &*state->value;
    }

    [[nodiscard]] auto loaded() const noexcept -> bool
    {
        return state_->value.has_value();
    }

private:
    struct state
    {
        state() = default;

        explicit state(loader_type value) : loader(std::move(value)) {}

        loader_type loader;
        std::optional<T> value;
        cnetmod::async_mutex mutex;
    };

    std::shared_ptr<state> state_;
};

// =============================================================================
// result_map_parser — Parse <resultMap> from XML
// =============================================================================

export class result_map_parser
{
public:
    static auto parse(const xml_node& node)
        -> std::expected<result_map_def, std::string>;

private:
    static auto parse_result_mapping(const xml_node& node) -> result_mapping;
    static auto parse_association(const xml_node& node) -> association;
    static auto parse_collection(const xml_node& node) -> collection;
};

// =============================================================================
// result_map_registry — Global registry for ResultMaps
// =============================================================================

export class result_map_registry
{
public:
    /// Register a ResultMap
    void register_result_map(result_map_def def);

    /// Find ResultMap by ID
    auto find(std::string_view id) const -> const result_map_def*;

    /// Parse and register ResultMap from XML node
    auto load_from_xml(const xml_node& node) -> std::expected<void, std::string>;

private:
    std::unordered_map<std::string, result_map_def> result_maps_;
};

// =============================================================================
// result_map_applier — Apply ResultMap to result set
// =============================================================================

export class result_map_applier
{
public:
    /// Apply ResultMap to a single row
    /// Returns a map of property -> value
    static auto apply_to_row(const result_map_def& result_map,
        const row& result_row,
        const std::vector<std::string>& column_names)
        -> std::unordered_map<std::string, param_value>;

    // Materialize one-to-one and one-to-many nested resultMaps from joined
    // rows. Parent and collection identity use <id> mappings for de-duplication.
    static auto materialize_joined(const result_map_def& result_map,
        const result_set& result, const result_map_registry& registry)
        -> std::vector<mapped_object>;

private:
    // Convert snake_case to camelCase
    static auto snake_to_camel(std::string_view snake) -> std::string;
};

// =============================================================================
// Global result_map_registry instance
// =============================================================================

export auto global_result_map_registry() -> result_map_registry&;

} // namespace cnetmod::orm
