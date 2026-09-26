module;

#include <cnetmod/config.hpp>

/**
 * @brief Bounded leases for provider-neutral chat models.
 */
export module cnetmod.application.chat_model_pool;

#ifdef CNETMOD_HAS_CHAT_MODEL
import std;
import cnetmod.ai;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::application {

class chat_model_pool_state;
export class chat_model_pool;

/**
 * @brief Exclusive RAII lease for one model connection.
 */
export class pooled_chat_model
{
public:
    pooled_chat_model() noexcept = default;
    pooled_chat_model(pooled_chat_model&& other) noexcept;
    auto operator=(pooled_chat_model&& other) noexcept -> pooled_chat_model&;
    ~pooled_chat_model();

    pooled_chat_model(const pooled_chat_model&) = delete;
    auto operator=(const pooled_chat_model&) -> pooled_chat_model& = delete;

    /**
     * @brief Returns the exclusively leased model.
     */
    [[nodiscard]] auto get() const noexcept -> ai::chat_model&;

    /**
     * @brief Reports whether this lease owns a model.
     */
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    pooled_chat_model(std::shared_ptr<chat_model_pool_state> state,
        std::shared_ptr<ai::chat_model> model) noexcept;
    void release() noexcept;

    std::shared_ptr<chat_model_pool_state> state_;
    std::shared_ptr<ai::chat_model> model_;

    friend class chat_model_pool;
};

/**
 * @brief Maintains a replaceable fixed-size pool of chat model connections.
 *
 * reset() publishes a new generation without invalidating outstanding leases.
 * close() wakes waiters and prevents returned leases from re-entering the pool.
 */
export class chat_model_pool
{
public:
    explicit chat_model_pool(io_context& io);
    chat_model_pool(io_context& io,
        std::vector<std::shared_ptr<ai::chat_model>> models);
    ~chat_model_pool();

    chat_model_pool(const chat_model_pool&) = delete;
    auto operator=(const chat_model_pool&) -> chat_model_pool& = delete;

    /**
     * @brief Waits for an exclusive model lease.
     */
    [[nodiscard]] auto acquire(cancel_token* cancellation = nullptr)
        -> task<std::expected<pooled_chat_model, std::error_code>>;

    /**
     * @brief Replaces the active generation with the supplied models.
     */
    [[nodiscard]] auto reset(
        std::vector<std::shared_ptr<ai::chat_model>> models)
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Stops admission and wakes all pending borrowers.
     */
    auto close() -> task<void>;

    /** Event loop that owns the pool and every leased model. */
    [[nodiscard]] auto event_loop() noexcept -> io_context& { return io_; }

private:
    io_context& io_;
    async_mutex state_gate_;
    std::shared_ptr<chat_model_pool_state> state_;
};

} // namespace cnetmod::application
#endif
