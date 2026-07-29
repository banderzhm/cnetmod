import cnetmod.testing.messaging.amqp10_interop_driver;
import std;
import cnetmod.core.net_init;

auto main(int argument_count, char** arguments) -> int
{
    if (argument_count != 2 || std::string_view(arguments[1]) != "--json-lines")
    {
        std::cerr << "usage: amqp10_interop_driver --json-lines\n";
        return 2;
    }
    cnetmod::net_init network;
    return cnetmod::testing::messaging::amqp10::run_json_lines(
        std::cin, std::cout, std::cerr);
}
