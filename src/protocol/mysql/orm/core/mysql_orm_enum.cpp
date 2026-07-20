module cnetmod.protocol.mysql;

import :orm_enum;

namespace cnetmod::mysql::orm {

auto global_enum_registry() -> enum_registry & {
  static enum_registry instance;
  return instance;
}

} // namespace cnetmod::mysql::orm
