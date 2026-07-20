module cnetmod.protocol.mqtt;

import :v5_packet;

namespace cnetmod::mqtt::v5 {

connect_packet::connect_packet(connect_options value)
    : options(std::move(value)) {
  options.version = protocol_version::v5;
}

} // namespace cnetmod::mqtt::v5
