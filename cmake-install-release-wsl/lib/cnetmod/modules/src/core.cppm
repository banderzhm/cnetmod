/// cnetmod.core — Core module aggregation
/// Import once to get all core functionality:
///   error / buffer / buffer_pool / address / socket / net_init /
///   file / serial_port / log / dns / crash_dump
///
/// Note: cnetmod.core.ssl needs separate import (requires #ifdef CNETMOD_HAS_SSL protection)

export module cnetmod.core;

export import cnetmod.core.error;
export import cnetmod.core.buffer;
export import cnetmod.core.buffer_pool;
export import cnetmod.core.address;
export import cnetmod.core.socket;
export import cnetmod.core.net_init;
export import cnetmod.core.file;
export import cnetmod.core.serial_port;
export import cnetmod.core.log;
export import cnetmod.core.dns;
export import cnetmod.core.crash_dump;
