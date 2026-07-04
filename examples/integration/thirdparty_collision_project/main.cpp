#include <leveldb/db.h>
#include <pugixml.hpp>
#include <stdexec/execution.hpp>

import std;
import cnetmod.core;
import cnetmod.io;

int main()
{
    pugi::xml_document doc;
    auto parsed = doc.load_string("<service><name>cnetmod</name></service>");
    if (!parsed)
        return 1;

    auto sender = stdexec::just(42);
    (void)sender;

    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();

    std::println("third-party collision integration OK");
    std::println("pugixml root={}", doc.child("service").child("name").text().as_string());
    std::println("leveldb version={}.{}", leveldb::kMajorVersion, leveldb::kMinorVersion);
    std::println("io_context ready={}", ctx != nullptr);
    return 0;
}
