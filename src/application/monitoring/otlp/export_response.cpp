module cnetmod.observability.export_response;

import std;
import nlohmann.json;

namespace cnetmod::observability::detail {
namespace {

    /**
     * @brief Reads only acknowledgement fields with bounded structural state.
     */
    class acknowledgement_reader
    {
        enum class field
        {
            ignored,
            partial,
            rejected,
            message
        };
        enum class container
        {
            ignored,
            root,
            partial
        };
        std::array<container, 16> stack_{};
        std::size_t depth_{};
        field next_{};
        std::string_view rejected_field_;
        bool root_seen_{};
        bool rejected_seen_{};
        bool message_seen_{};

        auto scalar() -> bool
        {
            return depth_ != 0 && next_ == field::ignored;
        }

        auto count(std::uint64_t value) -> bool
        {
            if (next_ != field::rejected)
                return scalar();
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                return false;
            result.rejected = value;
            next_ = field::ignored;
            return true;
        }

    public:
        export_acknowledgement result;

        explicit acknowledgement_reader(std::string_view name) : rejected_field_(name) {}

        auto null() -> bool
        {
            return scalar();
        }

        auto boolean(bool) -> bool
        {
            return scalar();
        }

        auto number_integer(std::int64_t value) -> bool
        {
            return next_ == field::rejected ? value >= 0 && count(static_cast<std::uint64_t>(value)) : scalar();
        }

        auto number_unsigned(std::uint64_t value) -> bool
        {
            return count(value);
        }

        auto number_float(double, const std::string&) -> bool
        {
            return scalar();
        }

        auto string(std::string& value) -> bool
        {
            if (next_ == field::message)
            {
                result.warning = !value.empty();
                next_ = field::ignored;
                return true;
            }
            if (next_ != field::rejected)
                return scalar();
            std::uint64_t parsed{};
            const auto converted = std::from_chars(value.data(), value.data() + value.size(), parsed);
            return converted.ec == std::errc{} && converted.ptr == value.data() + value.size() && count(parsed);
        }

        auto binary(nlohmann::json::binary_t&) -> bool
        {
            return false;
        }

        auto start_object(std::size_t) -> bool
        {
            if (depth_ == stack_.size())
                return false;
            auto role = container::ignored;
            if (depth_ == 0)
            {
                if (root_seen_)
                    return false;
                root_seen_ = true;
                role = container::root;
            }
            else if (next_ == field::partial)
                role = container::partial;
            else if (next_ != field::ignored)
                return false;
            stack_[depth_++] = role;
            next_ = field::ignored;
            return true;
        }

        auto key(std::string& name) -> bool
        {
            next_ = field::ignored;
            if (stack_[depth_ - 1] == container::root && name == "partialSuccess")
            {
                if (result.partial)
                    return false;
                result.partial = true;
                next_ = field::partial;
            }
            else if (stack_[depth_ - 1] == container::partial)
            {
                if (name == rejected_field_)
                {
                    if (std::exchange(rejected_seen_, true))
                        return false;
                    next_ = field::rejected;
                }
                else if (name == "errorMessage")
                {
                    if (std::exchange(message_seen_, true))
                        return false;
                    next_ = field::message;
                }
            }
            return true;
        }

        auto end_object() -> bool
        {
            --depth_;
            next_ = field::ignored;
            return true;
        }

        auto start_array(std::size_t) -> bool
        {
            if (!scalar() || depth_ == stack_.size())
                return false;
            stack_[depth_++] = container::ignored;
            return true;
        }

        auto end_array() -> bool
        {
            return end_object();
        }

        auto parse_error(std::size_t, const std::string&, const nlohmann::json::exception&) -> bool
        {
            return false;
        }
    };

} // namespace

auto parse_export_response(std::string_view body, std::string_view rejected_field,
    std::uint64_t sent) noexcept -> std::optional<export_acknowledgement>
{
    if (body.empty() || body.size() > 64U * 1024U)
        return std::nullopt;
    try
    {
        acknowledgement_reader reader{rejected_field};
        if (!nlohmann::json::sax_parse(body.begin(), body.end(), &reader) || reader.result.rejected > sent)
            return std::nullopt;
        return reader.result;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

} // namespace cnetmod::observability::detail
