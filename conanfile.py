from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy
import os


class CnetmodConan(ConanFile):
    name = "cnetmod"
    version = "2.0.0"
    description = "Cross-platform asynchronous network library with C++23 modules"
    homepage = "https://github.com/banderzhm/cnetmod"
    license = "MIT"
    package_type = "static-library"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [False],
        "fPIC": [True, False],
        "with_amqp091": [True, False],
        "with_amqp10": [True, False],
        "with_coap": [True, False],
        "with_dns": [True, False],
        "with_grpc": [True, False],
        "with_http": [True, False],
        "with_kafka": [True, False],
        "with_mail": [True, False],
        "with_modbus": [True, False],
        "with_mongodb": [True, False],
        "with_mqtt": [True, False],
        "with_mysql": [True, False],
        "with_openai": [True, False],
        "with_postgresql": [True, False],
        "with_raft": [True, False],
        "with_redis": [True, False],
        "with_socks5": [True, False],
        "with_websocket": [True, False],
        "with_orm": [True, False],
        "with_ssl": [True, False],
        "with_http2": [True, False],
        "with_lz4": [True, False],
        "with_leveldb": [True, False],
        "with_mimalloc": [True, False],
        "with_stdexec_package": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_amqp091": True,
        "with_amqp10": True,
        "with_coap": True,
        "with_dns": True,
        "with_grpc": True,
        "with_http": True,
        "with_kafka": True,
        "with_mail": True,
        "with_modbus": True,
        "with_mongodb": True,
        "with_mqtt": True,
        "with_mysql": True,
        "with_openai": True,
        "with_postgresql": True,
        "with_raft": True,
        "with_redis": True,
        "with_socks5": True,
        "with_websocket": True,
        "with_orm": True,
        "with_ssl": True,
        "with_http2": True,
        "with_lz4": True,
        "with_leveldb": True,
        "with_mimalloc": True,
        "with_stdexec_package": False,
        "jwt-cpp/*:with_picojson": False,
        "leveldb/*:shared": False,
        "leveldb/*:with_crc32c": False,
        "leveldb/*:with_snappy": False,
        "pugixml/*:shared": False,
    }

    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "src/*",
        "3rdparty/pugixml/*",
        "3rdparty/stdexec/include/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        self.options.shared = False

    def requirements(self):
        self.requires("nlohmann_json/3.12.0")

        if self.options.with_http:
            self.requires("jwt-cpp/0.7.2")
        if self.options.with_postgresql:
            self.requires("icu/[>=74 <79]")
        if self.options.with_orm:
            self.requires("pugixml/1.16")
        if (
            self.options.with_http
            or self.options.with_grpc
            or self.options.with_kafka
            or self.options.with_mongodb
        ):
            self.requires("zlib/1.3.2")
        if self.options.with_kafka and self.options.with_lz4:
            self.requires("lz4/[>=1.9 <2]")

        if self.options.with_ssl:
            self.requires("openssl/[>=1.1 <4]")
        if self.options.with_raft and self.options.with_leveldb:
            self.requires("leveldb/1.23")
        if self.options.with_mimalloc:
            self.requires("mimalloc/3.3.2")
        if self.options.with_stdexec_package:
            self.requires("p2300/[>=0.0.0]")
        if self.settings.os == "Linux":
            self.requires("liburing/2.13")

    def validate(self):
        check_min_cppstd(self, "23")
        dependencies = {
            "with_dns": ("with_http",),
            "with_grpc": ("with_http",),
            "with_mqtt": ("with_http", "with_websocket"),
            "with_openai": ("with_http",),
            "with_websocket": ("with_http",),
        }
        for protocol, required_protocols in dependencies.items():
            if not bool(self.options.get_safe(protocol)):
                continue
            missing = [
                dependency
                for dependency in required_protocols
                if not bool(self.options.get_safe(dependency))
            ]
            if missing:
                raise ConanInvalidConfiguration(
                    f"{protocol}=True requires {', '.join(missing)}=True"
                )

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.variables["CNETMOD_USE_SYSTEM_DEPS"] = True
        # Every Conan protocol option is explicit, so do not let CMake's
        # aggregate default silently re-enable an omitted protocol.
        tc.variables["CNETMOD_ENABLE_ALL_PROTOCOLS"] = False
        protocol_options = {
            "AMQP091": "with_amqp091",
            "AMQP10": "with_amqp10",
            "COAP": "with_coap",
            "DNS": "with_dns",
            "GRPC": "with_grpc",
            "HTTP": "with_http",
            "KAFKA": "with_kafka",
            "MAIL": "with_mail",
            "MODBUS": "with_modbus",
            "MONGODB": "with_mongodb",
            "MQTT": "with_mqtt",
            "MYSQL": "with_mysql",
            "OPENAI": "with_openai",
            "POSTGRESQL": "with_postgresql",
            "RAFT": "with_raft",
            "REDIS": "with_redis",
            "SOCKS5": "with_socks5",
            "WEBSOCKET": "with_websocket",
        }
        for protocol, option_name in protocol_options.items():
            tc.variables[f"CNETMOD_ENABLE_{protocol}"] = bool(
                self.options.get_safe(option_name)
            )
        tc.variables["CNETMOD_ENABLE_ORM"] = bool(self.options.with_orm)
        tc.variables["CNETMOD_ENABLE_SSL"] = bool(self.options.with_ssl)
        tc.variables["CNETMOD_ENABLE_HTTP2"] = bool(self.options.with_http2)
        tc.variables["CNETMOD_ENABLE_LZ4"] = bool(self.options.with_lz4)
        tc.variables["CNETMOD_ENABLE_LEVELDB"] = bool(self.options.with_leveldb)
        tc.variables["CNETMOD_USE_MIMALLOC"] = bool(self.options.with_mimalloc)
        tc.variables["CNETMOD_BUILD_TESTS"] = False
        tc.variables["CNETMOD_BUILD_BENCH"] = False
        tc.variables["CNETMOD_BUILD_EXAMPLES"] = False
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build(target="cnetmod_core")

    def package(self):
        copy(self, "LICENSE*", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "*.hpp", os.path.join(self.source_folder, "include"), os.path.join(self.package_folder, "include"))
        copy(self, "*.cppm", os.path.join(self.source_folder, "src"), os.path.join(self.package_folder, "src"))
        copy(self, "*.cppm", os.path.join(self.source_folder, "cmake", "modules"), os.path.join(self.package_folder, "cmake", "modules"))
        copy(self, "*.lib", self.build_folder, os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", self.build_folder, os.path.join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "cnetmod")
        self.cpp_info.set_property("cmake_target_name", "cnetmod::cnetmod_core")
        self.cpp_info.libs = ["cnetmod_core"]
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.builddirs = ["cmake"]
