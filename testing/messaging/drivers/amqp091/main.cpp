import std;
import cnetmod.core.net_init;
import cnetmod.testing.messaging.amqp091_driver;

auto main(int argc, char** argv) -> int
{
    if (argc != 2 || std::string_view{argv[1]} != "--json-lines")
    {
        std::println(std::cerr, "usage: amqp091_interop_driver --json-lines");
        return 2;
    }
    cnetmod::net_init network;
    return cnetmod::testing::messaging::amqp091_driver::run_json_lines(
        std::cin, std::cout, std::cerr);
}
