from conan import ConanFile
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
        "with_ssl": [True, False],
        "with_http2": [True, False],
        "with_leveldb": [True, False],
        "with_mimalloc": [True, False],
        "with_stdexec_package": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_ssl": True,
        "with_http2": True,
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
        self.requires("jwt-cpp/0.7.2")
        self.requires("nlohmann_json/3.12.0")
        self.requires("pugixml/1.16")
        self.requires("zlib/1.3.2")

        if self.options.with_ssl:
            self.requires("openssl/[>=1.1 <4]")
        if self.options.with_leveldb:
            self.requires("leveldb/1.23")
        if self.options.with_mimalloc:
            self.requires("mimalloc/3.3.2")
        if self.options.with_stdexec_package:
            self.requires("p2300/[>=0.0.0]")
        if self.settings.os == "Linux":
            self.requires("liburing/2.13")

    def validate(self):
        check_min_cppstd(self, "23")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.variables["CNETMOD_USE_SYSTEM_DEPS"] = True
        tc.variables["CNETMOD_ENABLE_SSL"] = bool(self.options.with_ssl)
        tc.variables["CNETMOD_ENABLE_HTTP2"] = bool(self.options.with_http2)
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
