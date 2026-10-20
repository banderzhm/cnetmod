// External allocation-balance fixture only. No network, timer, or production hook.
import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.protocol.quic;

namespace {
struct allocation { void* address{}; std::size_t size{}; bool released{}; };
allocation allocations[65536];
std::size_t used{}, overflow{}, unknown_frees{}, double_frees{};
thread_local bool tracking{};
void allocated(void* address, std::size_t size) noexcept
{
    if (!tracking || address == nullptr) return;
    if (used == std::size(allocations)) { ++overflow; return; }
    allocations[used++] = {address, size, false};
}
void released(void* address) noexcept
{
    if (!tracking || address == nullptr) return;
    for (auto index = used; index != 0U;)
    {
        auto& row = allocations[--index];
        if (row.address == address && !row.released) { row.released = true; return; }
    }
    // An allocation made outside this measured scope can legally be freed
    // here. Record it separately rather than treating it as matched progress.
    ++unknown_frees;
}
}

extern "C" {
void* __real__Znwm(std::size_t);
void* __real__Znam(std::size_t);
void* __real__ZnwmSt11align_val_t(std::size_t, std::align_val_t);
void* __real__ZnamSt11align_val_t(std::size_t, std::align_val_t);
void __real__ZdlPv(void*) noexcept;
void __real__ZdaPv(void*) noexcept;
void __real__ZdlPvm(void*, std::size_t) noexcept;
void __real__ZdaPvm(void*, std::size_t) noexcept;
void __real__ZdlPvSt11align_val_t(void*, std::align_val_t) noexcept;
void __real__ZdaPvSt11align_val_t(void*, std::align_val_t) noexcept;
void __real__ZdlPvmSt11align_val_t(void*, std::size_t, std::align_val_t) noexcept;
void __real__ZdaPvmSt11align_val_t(void*, std::size_t, std::align_val_t) noexcept;
void* __wrap__Znwm(std::size_t n) { auto* p=__real__Znwm(n); allocated(p,n); return p; }
void* __wrap__Znam(std::size_t n) { auto* p=__real__Znam(n); allocated(p,n); return p; }
void* __wrap__ZnwmSt11align_val_t(std::size_t n,std::align_val_t a) { auto* p=__real__ZnwmSt11align_val_t(n,a); allocated(p,n); return p; }
void* __wrap__ZnamSt11align_val_t(std::size_t n,std::align_val_t a) { auto* p=__real__ZnamSt11align_val_t(n,a); allocated(p,n); return p; }
void __wrap__ZdlPv(void* p) noexcept { released(p); __real__ZdlPv(p); }
void __wrap__ZdaPv(void* p) noexcept { released(p); __real__ZdaPv(p); }
void __wrap__ZdlPvm(void* p,std::size_t n) noexcept { released(p); __real__ZdlPvm(p,n); }
void __wrap__ZdaPvm(void* p,std::size_t n) noexcept { released(p); __real__ZdaPvm(p,n); }
void __wrap__ZdlPvSt11align_val_t(void* p,std::align_val_t a) noexcept { released(p); __real__ZdlPvSt11align_val_t(p,a); }
void __wrap__ZdaPvSt11align_val_t(void* p,std::align_val_t a) noexcept { released(p); __real__ZdaPvSt11align_val_t(p,a); }
void __wrap__ZdlPvmSt11align_val_t(void* p,std::size_t n,std::align_val_t a) noexcept { released(p); __real__ZdlPvmSt11align_val_t(p,n,a); }
void __wrap__ZdaPvmSt11align_val_t(void* p,std::size_t n,std::align_val_t a) noexcept { released(p); __real__ZdaPvmSt11align_val_t(p,n,a); }
}

auto main() -> int
{
    cnetmod::net_init net;
    logger::init("quic-cache-balance", logger::level::info);
    namespace q = cnetmod::quic;
    const q::quic_config config;
    bool passed = true;
    for (const bool recovery : {false, true})
    for (const std::size_t count : {std::size_t{1},std::size_t{2},std::size_t{64},std::size_t{128}})
    {
        used=overflow=unknown_frees=double_frees=0;
        bool ack_passed=true;
        tracking=true;
        for (unsigned round{}; round<32U; ++round)
        {
            const auto now=std::chrono::steady_clock::now();
            q::ack_frame ack;
            ack.largest_acked=count-1;
            ack.first_ack_range=count-1;
            if (recovery)
            {
                q::quic_recovery_controller owner(config);
                for (std::size_t pn{};pn<count;++pn)
                    owner.on_packet_sent(pn,1200U,now,true,q::pn_space::application);
                const auto result=owner.on_ack_received(ack,now+std::chrono::milliseconds{10},q::pn_space::application);
                ack_passed=ack_passed && bool(result);
            }
            else
            {
                q::loss_detector owner(config);
                for (std::size_t pn{};pn<count;++pn)
                    owner.on_packet_sent(pn,1200U,now,true,q::pn_space::application);
                const auto result=owner.on_ack_received(ack,count-1,now+std::chrono::milliseconds{10},q::pn_space::application);
                ack_passed=ack_passed && bool(result) && result->size()==count;
            }
        }
        tracking=false;
        std::size_t outstanding{}, bytes{}, node72{}, node216{};
        for (std::size_t index{};index<used;++index)
        {
            const auto& row=allocations[index];
            if (!row.released) { ++outstanding; bytes+=row.size; }
            node72+=row.size==72U;
            node216+=row.size==216U;
        }
        const auto expected_nodes=32U*count;
        const bool captured=node72>=expected_nodes && (!recovery || node216>=expected_nodes);
        const bool ok=ack_passed && captured && outstanding==0U && overflow==0U;
        logger::info{"balance recovery={} packets={} rounds=32 captured={} ack={} allocations={} node72={} node216={} outstanding={} bytes={} unknown_frees={} overflow={} pass={}",
            recovery,count,captured,ack_passed,used,node72,node216,outstanding,bytes,unknown_frees,overflow,ok};
        if (!ok)
            for (std::size_t index{},shown{};index<used && shown<8U;++index)
                if (!allocations[index].released)
                { logger::warn{"unreleased size={} address={}",allocations[index].size,allocations[index].address}; ++shown; }
        passed=passed && ok;
    }
    logger::shutdown();
    return passed?0:1;
}
