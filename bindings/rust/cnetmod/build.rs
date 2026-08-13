use std::env;

fn main() {
    println!("cargo:rerun-if-env-changed=CNETMOD_C_API_LIB_DIR");
    println!("cargo:rerun-if-env-changed=CNETMOD_C_API_LINK_SEARCH");
    println!("cargo:rerun-if-env-changed=CNETMOD_C_API_LINK_LIBS");
    if let Ok(directory) = env::var("CNETMOD_C_API_LIB_DIR") {
        println!("cargo:rustc-link-search=native={directory}");
        println!("cargo:rustc-link-lib=static=cnetmod_c");
        if let Some(paths) = env::var_os("CNETMOD_C_API_LINK_SEARCH") {
            for directory in env::split_paths(&paths) {
                println!("cargo:rustc-link-search=native={}", directory.display());
            }
        }
        // CMake owns the complete transitive dependency graph. Consumers that
        // link the static ABI directly may provide comma-separated additional
        // native libraries here (for example ssl,crypto,ws2_32 on Windows).
        if let Ok(libraries) = env::var("CNETMOD_C_API_LINK_LIBS") {
            for library in libraries
                .split(',')
                .map(str::trim)
                .filter(|name| !name.is_empty())
            {
                println!("cargo:rustc-link-lib={library}");
            }
        }
    }
}
