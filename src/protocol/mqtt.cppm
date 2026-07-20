/// cnetmod.protocol.mqtt — Aggregation module
/// Unified export of mqtt submodules

export module cnetmod.protocol.mqtt;

// Phase 1: Client
// The common model and wire codec live under mqtt/common.  Their stable
// partition names remain :types and :codec for source compatibility.
export import :types;
export import :codec;
// Versioned wire boundaries.  Generic codec remains the compatibility facade;
// new code chooses v3 or v5 explicitly.
export import :v3_packet;
export import :v3_codec;
export import :v5_packet;
export import :v5_codec;
export import :parser;
export import :client;

// Phase 2: Broker / Server
export import :topic_filter;
export import :session;
export import :retained;
export import :topic_alias;
export import :shared_sub;
export import :subscription_map;
export import :security;
export import :broker;
export import :ws_transport;
export import :persistence;
export import :sync_client;
