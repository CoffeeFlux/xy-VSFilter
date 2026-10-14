// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic adapter checks only; executes no pinned native renderer/history.
#include "document_admission.h"
#include "../../include/xy_rust_document.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using DocumentAdmissionProbe::Gate;
static void check(bool value) {
    if(!value) throw std::runtime_error("document admission assertion failed");
}
static void ok(uint32_t status) {
    // Busy is a failed PRIVATE probe outcome, never a public HRESULT conversion.
    check(status == XY_DOCUMENT_V1_OK);
}
class Signal {
    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false;
public:
    void Set() {std::lock_guard<std::mutex> guard(mutex);ready=true;changed.notify_all();}
    void Wait() {
        std::unique_lock<std::mutex> guard(mutex);
        if(!changed.wait_for(guard,std::chrono::seconds(5),[&]{return ready;}))
            throw std::runtime_error("document admission worker timeout");
    }
};
class Worker {
    std::exception_ptr failure;
    std::thread thread;
public:
    template<class Call> explicit Worker(Call call) : thread([this,call]{try{call();}catch(...){failure=std::current_exception();}}) {}
    ~Worker() {if(thread.joinable())thread.join();}
    void Join() {thread.join();if(failure)std::rethrow_exception(failure);}
};
static uint64_t u64(const uint8_t* bytes) {
    uint64_t result=0;for(unsigned i=0;i<8;i++)result|=uint64_t(bytes[i])<<(i*8);return result;
}
static void put32(std::vector<uint8_t>& bytes,size_t offset,uint32_t value) {
    for(unsigned i=0;i<4;i++)bytes[offset+i]=uint8_t(value>>(i*8));
}
static uint64_t create(Gate& gate) {
    std::array<uint8_t,8> bytes{};
    ok(gate.Invoke([&]{return xy_vsfilter_document_create_v1(bytes.data(),bytes.size());}));
    return u64(bytes.data());
}
static void count(Gate& gate,uint64_t owner) {
    std::array<uint8_t,8> bytes{};
    ok(gate.Invoke([&]{return xy_vsfilter_document_counts_v1(owner,bytes.data(),bytes.size());}));
}
static uint64_t populate_and_snapshot(Gate& gate,uint64_t owner) {
    // Prepared entry wire: start 0, end 10, Unicode true, one raw text unit.
    std::vector<uint8_t> entry(XY_DOCUMENT_V1_ENTRY_HEADER_BYTES+2,0);
    entry[8]=10;put32(entry,36,uint32_t(-1));put32(entry,40,1);put32(entry,44,1);
    entry[64]=0;entry[65]=0xd8;
    std::array<uint8_t,8> result{};
    ok(gate.Invoke([&]{return xy_vsfilter_document_add_prepared_v1(owner,0,entry.data(),entry.size(),result.data(),result.size());}));
    check(result[0]==1);
    ok(gate.Invoke([&]{return xy_vsfilter_document_snapshot_open_v1(owner,0,0,result.data(),result.size());}));
    return u64(result.data());
}
static std::vector<uint8_t> read_snapshot(Gate& gate,uint64_t snapshot) {
    std::array<uint8_t,8> size{};
    ok(gate.Invoke([&]{return xy_vsfilter_document_snapshot_size_v1(snapshot,size.data(),size.size());}));
    check(u64(size.data())<=1024);
    // Native output allocation happens after the preceding gate/admission ends.
    std::vector<uint8_t> bytes(static_cast<size_t>(u64(size.data())));
    ok(gate.Invoke([&]{return xy_vsfilter_document_snapshot_read_v1(snapshot,0,bytes.data(),bytes.size());}));
    return bytes;
}
static void parallel_calls(Gate& gate) {
    const auto first=create(gate),second=create(gate);
    Signal first_ready,second_ready,start;
    Worker left([&]{first_ready.Set();start.Wait();for(int i=0;i<64;i++)count(gate,first);});
    Worker right([&]{second_ready.Set();start.Wait();for(int i=0;i<64;i++)count(gate,second);});
    first_ready.Wait();second_ready.Wait();start.Set();left.Join();right.Join();
    ok(gate.Invoke([&]{return xy_vsfilter_document_copy_storage_v1(second,first);}));
    ok(gate.Invoke([&]{return xy_vsfilter_document_destroy_v1(first);}));
    ok(gate.Invoke([&]{return xy_vsfilter_document_destroy_v1(second);}));
}
static void close_during_call(Gate& gate) {
    const auto owner=create(gate),snapshot=populate_and_snapshot(gate,owner);
    const auto before=read_snapshot(gate,snapshot);
    Signal admitted,release,closing;
    std::atomic<bool> closed{false};
    Worker reader([&]{
        std::array<uint8_t,8> bytes{};
        // Test-only rendezvous while holding the NATIVE gate, before invoking
        // Rust. No wait is performed inside a Rust loan or production adapter.
        ok(gate.Invoke([&]{admitted.Set();release.Wait();return xy_vsfilter_document_counts_v1(owner,bytes.data(),bytes.size());}));
    });
    admitted.Wait();
    Worker destroyer([&]{
        // Observe failed acquisition on this thread while the reader owns the
        // gate, before announcing readiness for the blocking destroy call.
        check(!gate.ProbeCanAcquire());
        closing.Set();
        ok(gate.Invoke([&]{return xy_vsfilter_document_destroy_v1(owner);}));
        closed.store(true);
    });
    closing.Wait();check(!closed.load());release.Set();reader.Join();destroyer.Join();check(closed.load());
    std::array<uint8_t,8> bytes{};
    check(gate.Invoke([&]{return xy_vsfilter_document_counts_v1(owner,bytes.data(),bytes.size());})==XY_DOCUMENT_V1_BAD_HANDLE);
    check(read_snapshot(gate,snapshot)==before);
    ok(gate.Invoke([&]{return xy_vsfilter_document_snapshot_drop_v1(snapshot);}));
}
static void post_call_reentry(Gate& gate) {
    const auto owner=create(gate);
    count(gate,owner);
    // Stand-in notification executes after Invoke releases gate and Rust loan.
    auto notification=[&]{count(gate,owner);};notification();
    // Recursive native acquisition is also safe AFTER the inner Rust call ends.
    ok(gate.Invoke([&]{count(gate,owner);count(gate,owner);return uint32_t(XY_DOCUMENT_V1_OK);}));
    ok(gate.Invoke([&]{return xy_vsfilter_document_destroy_v1(owner);}));
}
int main() {
    try {
        Gate gate;
        parallel_calls(gate);
        close_during_call(gate);
        post_call_reentry(gate);
#ifdef _WIN32
        std::cout<<"document-admission-v1 complete gate=Windows-CRITICAL_SECTION checks=3 normal_busy=0\n";
#else
        std::cout<<"document-admission-v1 complete gate=portable-recursive-mutex checks=3 normal_busy=0 Windows-validation=pending\n";
#endif
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
