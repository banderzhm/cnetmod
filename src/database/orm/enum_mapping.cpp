module cnetmod.orm.enum_mapping;

namespace cnetmod::orm {

auto global_enum_registry() -> enum_registry&
{
    static enum_registry instance;
    return instance;
}

} // namespace cnetmod::orm
