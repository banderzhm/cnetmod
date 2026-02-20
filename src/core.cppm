/// cnetmod.core — 核心模块聚合
/// 一次导入即可获得所有核心功能：
///   error / buffer / buffer_pool / address / socket / net_init /
///   file / serial_port / log / dns / crash_dump
///
/// 注意：cnetmod.core.ssl 需单独导入（需要 #ifdef CNETMOD_HAS_SSL 保护）

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
