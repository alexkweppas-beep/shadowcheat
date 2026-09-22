#include "FeatureBundles.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "CloudStorage.h"
#include "../vmp_markers.hpp"

namespace FeatureBundles {
namespace {
struct Slot final {
    volatile LONG state{static_cast<LONG>(State::Idle)};
    volatile ULONGLONG retryAt{};
    SRWLOCK lock = SRWLOCK_INIT;
    SecureScriptBlob::Plaintext source;
};

Slot g_Slots[static_cast<std::size_t>(Id::Count)]{};
volatile LONG g_ShuttingDown = FALSE;

std::size_t Index(Id id) noexcept {
    return static_cast<std::size_t>(id);
}

const char* Name(Id id) noexcept {
    switch (id) {
    case Id::FiveRp: return "five_rp";
    case Id::Quant: return "quant";
    case Id::Ukraine: return "ukraine";
    case Id::Policy: return "policy";
    default: return "";
    }
}

void SetState(Slot& slot, State state) noexcept {
    InterlockedExchange(&slot.state, static_cast<LONG>(state));
}

void ExpirePlaintext(Slot& slot) noexcept {
    Sleep(2000);
    const LONG ready = static_cast<LONG>(State::Ready);
    if (InterlockedCompareExchange(
            &slot.state,
            static_cast<LONG>(State::Failed),
            ready) != ready)
        return;
    AcquireSRWLockExclusive(&slot.lock);
    slot.source.Clear();
    ReleaseSRWLockExclusive(&slot.lock);
    slot.retryAt = GetTickCount64() + 1000;
}

void ClearEncrypted(
    CloudStorage::EncryptedFeatureBundle& bundle) noexcept {
    if (!bundle.payload.empty())
        SecureZeroMemory(bundle.payload.data(), bundle.payload.size());
    bundle.payload.clear();
    SecureZeroMemory(bundle.key.data(), bundle.key.size());
    SecureZeroMemory(bundle.canary, sizeof(bundle.canary));
}

bool ContainsCanary(
    const SecureScriptBlob::Plaintext& plaintext,
    const char* canary) noexcept {
    if (!plaintext.Data() || !canary || strlen(canary) != 64)
        return false;
    const auto* begin = reinterpret_cast<const std::uint8_t*>(
        plaintext.Data());
    const auto* end = begin + plaintext.Size();
    const auto* marker = reinterpret_cast<const std::uint8_t*>(canary);
    return std::search(begin, end, marker, marker + 64) != end;
}

void PublishPlaintext(
    Slot& slot,
    SecureScriptBlob::Plaintext&& plaintext) noexcept {
    AcquireSRWLockExclusive(&slot.lock);
    slot.source = std::move(plaintext);
    ReleaseSRWLockExclusive(&slot.lock);
    slot.retryAt = 0;
    SetState(slot, State::Ready);
}

void Download(Id id) noexcept {
    Slot& slot = g_Slots[Index(id)];
    CloudStorage::EncryptedFeatureBundle encrypted;
    SecureScriptBlob::Plaintext plaintext;
    const bool downloaded =
        CloudStorage::DownloadFeatureBundle(Name(id), encrypted);
    bool decrypted = false;
    SHADOW_PROTECT_ULTRA("Feature.Bundle");
    if (downloaded) {
        decrypted = SecureScriptBlob::DecryptEnvelope(
            encrypted.payload,
            encrypted.key,
            plaintext);
        decrypted = decrypted &&
            ContainsCanary(plaintext, encrypted.canary);
    }
    SHADOW_PROTECT_END();
    ClearEncrypted(encrypted);
    if (!decrypted ||
        InterlockedCompareExchange(&g_ShuttingDown, FALSE, FALSE)) {
        slot.retryAt = GetTickCount64() + 5000;
        SetState(slot, State::Failed);
        return;
    }
    PublishPlaintext(slot, std::move(plaintext));
    ExpirePlaintext(slot);
}

DWORD WINAPI DownloadWorker(void* context) noexcept {
    Download(static_cast<Id>(reinterpret_cast<std::uintptr_t>(context)));
    return 0;
}
}

void Request(Id id) noexcept {
    if (id >= Id::Count ||
        InterlockedCompareExchange(&g_ShuttingDown, FALSE, FALSE))
        return;
    Slot& slot = g_Slots[Index(id)];
    if (slot.retryAt && GetTickCount64() < slot.retryAt)
        return;
    LONG expected = static_cast<LONG>(State::Idle);
    if (InterlockedCompareExchange(
            &slot.state,
            static_cast<LONG>(State::Loading),
            expected) != expected) {
        expected = static_cast<LONG>(State::Failed);
        if (InterlockedCompareExchange(
                &slot.state,
                static_cast<LONG>(State::Loading),
                expected) != expected)
            return;
    }
    if (!QueueUserWorkItem(
            DownloadWorker,
            reinterpret_cast<void*>(Index(id)),
            WT_EXECUTEDEFAULT)) {
        SetState(slot, State::Failed);
    }
}

bool Consume(
    Id id,
    SecureScriptBlob::Plaintext& output) noexcept {
    if (GetState(id) != State::Ready)
        return false;
    Slot& slot = g_Slots[Index(id)];
    AcquireSRWLockExclusive(&slot.lock);
    output = std::move(slot.source);
    ReleaseSRWLockExclusive(&slot.lock);
    SetState(slot, State::Idle);
    return output.Data() != nullptr;
}

State GetState(Id id) noexcept {
    if (id >= Id::Count)
        return State::Failed;
    return static_cast<State>(InterlockedCompareExchange(
        &g_Slots[Index(id)].state,
        0,
        0));
}

void Shutdown() noexcept {
    InterlockedExchange(&g_ShuttingDown, TRUE);
    for (auto& slot : g_Slots) {
        AcquireSRWLockExclusive(&slot.lock);
        slot.source.Clear();
        ReleaseSRWLockExclusive(&slot.lock);
        SetState(slot, State::Failed);
    }
}
}
