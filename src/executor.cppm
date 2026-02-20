/// cnetmod.executor — 执行器模块聚合
/// 一次导入即可获得所有异步执行器功能：
///   async_op / scheduler / pool

export module cnetmod.executor;

export import cnetmod.executor.async_op;
export import cnetmod.executor.scheduler;
export import cnetmod.executor.pool;
