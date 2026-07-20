module cnetmod.protocol.mqtt;

import :v3_packet;

namespace cnetmod::mqtt::v3 {

connect_packet::connect_packet(connect_options value)
    : options(std::move(value)) {
  // A versioned packet is the boundary that prevents a v3 connection from
  // accidentally emitting the v5 wire format.
  options.version = protocol_version::v3_1_1;
  options.props.clear();
}

} // namespace cnetmod::mqtt::v3
