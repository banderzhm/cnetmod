import std;
import cnetmod.core;
import cnetmod.core.log;

extern "C" auto cnetmod_test_in_place_crypto() -> int;

auto main() -> int
{
    cnetmod::net_init net;
    logger::init("in-place-crypto-regression", logger::level::info);
    const auto result = cnetmod_test_in_place_crypto();
    logger::shutdown();
    return result;
}
