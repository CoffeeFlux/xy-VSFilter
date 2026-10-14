// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// ISOLATED TEST ADAPTER ONLY. No production callers or public status mapping.
#include <stdint.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <mutex>
#endif

namespace DocumentAdmissionProbe {
class Gate {
#ifdef _WIN32
    CRITICAL_SECTION section;
#else
    std::recursive_mutex section;
#endif
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    void Lock() {
#ifdef _WIN32
        EnterCriticalSection(&section);
#else
        section.lock();
#endif
    }
    void Unlock() {
#ifdef _WIN32
        LeaveCriticalSection(&section);
#else
        section.unlock();
#endif
    }
    struct Loan {
        Gate& gate;
        explicit Loan(Gate& value) : gate(value) { gate.Lock(); }
        ~Loan() { gate.Unlock(); }
        Loan(const Loan&) = delete;
        Loan& operator=(const Loan&) = delete;
    };
public:
    Gate() {
#ifdef _WIN32
        InitializeCriticalSection(&section);
#endif
    }
    ~Gate() {
#ifdef _WIN32
        DeleteCriticalSection(&section);
#endif
    }
    // TEST INSTRUMENTATION ONLY: establish observed contention without sleeps.
    // A successful attempt is immediately released and creates no Rust loan.
    bool ProbeCanAcquire() {
#ifdef _WIN32
        if(!TryEnterCriticalSection(&section)) return false;
#else
        if(!section.try_lock()) return false;
#endif
        Unlock();
        return true;
    }
    // Production adaptation would supply exactly one private C ABI call here.
    // The returned value remains a PRIVATE status; Busy is never mapped/retried.
    template<class Call> uint32_t Invoke(Call call) {
        Loan loan(*this);
        return call();
    }
};
}
