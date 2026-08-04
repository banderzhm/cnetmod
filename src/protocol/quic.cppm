module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic;

import std;

// Only import QUIC modules if enabled
#ifdef CNETMOD_HAS_QUIC

// Basic types and utilities
export import :types;

// Protocol primitives
export import :varint;
export import :frame;
export import :packet;

// Core QUIC functionality
export import :crypto;
export import :connection;
export import :stream;

// Reliability mechanisms
export import :loss_detection;
export import :congestion_control;
export import :flow_control;

#else

// When QUIC is disabled, still export an empty module stub
// This prevents missing symbol errors for consumers
namespace cnetmod::quic {
    // Empty namespace for when QUIC is disabled
}
#endif
