/// cnetmod.protocol.openai:methods — Strongly typed AI service operations

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:methods;

import std;
import cnetmod.coro.task;
import :chat;
import :model;
import :service;
import :structured;

namespace cnetmod::openai {

export template <typename Request, typename Response>
struct service_method_contract
{
    std::function<std::expected<std::string, std::string>(const Request&)>
        render_input;
    std::function<std::string(const Request&)> resolve_session;
    structured_output_contract<Response> output;
    chat_request defaults;
};

/// Typed application operation analogous to a declarative AI Service method.
export template <typename Request, typename Response>
class service_method
{
public:
    service_method(ai_service& service,
        service_method_contract<Request, Response> contract)
        : structured_(service, std::move(contract.output)),
          render_input_(std::move(contract.render_input)),
          resolve_session_(std::move(contract.resolve_session)),
          defaults_(std::move(contract.defaults))
    {
        if (!render_input_)
            throw std::invalid_argument(
                "service method input renderer cannot be empty");
    }

    auto invoke(const Request& request, const run_config& config = {})
        -> task<std::expected<structured_service_result<Response>, std::string>>
    {
        auto input = render_input_(request);
        if (!input)
            co_return std::unexpected("service method input failed: " +
                input.error());
        auto session = resolve_session_ ? resolve_session_(request)
                                        : std::string{};
        co_return co_await structured_.invoke(std::move(*input),
            std::move(session), defaults_, config);
    }

private:
    structured_service<Response> structured_;
    std::function<std::expected<std::string, std::string>(const Request&)>
        render_input_;
    std::function<std::string(const Request&)> resolve_session_;
    chat_request defaults_;
};

} // namespace cnetmod::openai
