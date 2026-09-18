module;

#include <cnetmod/config.hpp>

/**
 * @brief Provider-neutral artificial intelligence contracts.
 */
export module cnetmod.ai;

#ifdef CNETMOD_HAS_CHAT_MODEL
import cnetmod.protocol.openai;

export namespace cnetmod::ai {

using json = openai::json;
using usage = openai::usage;
using message = openai::message;
using chat_request = openai::chat_request;
using chat_chunk = openai::chat_chunk;
using chat_response = openai::chat_response;
using run_event_type = openai::run_event_type;
using run_event = openai::run_event;
using run_callback = openai::run_callback;
using run_listener = openai::run_listener;
using functional_run_listener = openai::functional_run_listener;
using run_config = openai::run_config;
using chat_model = openai::chat_model;
using conversation_store = openai::append_only_chat_memory_store;
using in_memory_conversation_store =
    openai::in_memory_append_only_chat_memory_store;
using conversation_options = openai::memory_options;

} // namespace cnetmod::ai
#endif
