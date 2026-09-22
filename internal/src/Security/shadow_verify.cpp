#include "shadow_verify.hpp"
#include "build_identity.hpp"
#include "../shared_ipc.hpp"
#include "../vmp_markers.hpp"
#include "../Features/CloudStorage.h"
#include "../Main/Hooks.h"
#include <thread>
#include <chrono>

namespace {
bool HasValidFields(const ShadowLaunchAuth* auth) noexcept {
    return auth &&
        auth->magic == SHADOW_LAUNCH_AUTH_MAGIC &&
        auth->version == SHADOW_LAUNCH_AUTH_VERSION &&
        auth->size == sizeof(ShadowLaunchAuth) &&
        auth->nonce != 0 &&
        auth->launch_ticket[0] != 0 &&
        auth->artifact_hash[0] != 0 &&
        BuildIdentity::MatchesVariant(auth->variant_id) &&
        BuildIdentity::ValidateVariantNonce(auth->nonce) &&
        BuildIdentity::MatchesCustomerTag(auth->customer_tag);
}

bool IsRecent(const ShadowLaunchAuth& auth) noexcept {
    const ULONGLONG now = GetTickCount64();
    return now >= auth.issued_tick &&
        now - auth.issued_tick <= 30000;
}

bool IsLoaderAlive(DWORD loaderPid) noexcept {
    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        loaderPid);
    DWORD exitCode = 0;
    const bool alive = process &&
        GetExitCodeProcess(process, &exitCode) &&
        exitCode == STILL_ACTIVE;
    if (process)
        CloseHandle(process);
    return alive;
}
}

extern "C" {

bool ShadowSecurity_VerifyLaunchAuth_impl(const ShadowLaunchAuth* auth) {
    SHADOW_PROTECT_VIRT("VerifyLaunchAuth");
    const bool valid = HasValidFields(auth) &&
        IsRecent(*auth) &&
        IsLoaderAlive(auth->loader_pid);
    SHADOW_PROTECT_END();
    return valid;
}

void ShadowSecurity_StartPeriodicSubscriptionCheck_impl() {
    std::thread([]() {
        auto retryDelay = std::chrono::seconds(1);
        while (true) {
            const auto result =
                CloudStorage::VerifySubscription();
            if (result ==
                CloudStorage::VerificationResult::Denied) {
                hooks::RequestUnload();
                return;
            }
            if (result ==
                CloudStorage::VerificationResult::Authorized) {
                retryDelay = std::chrono::seconds(1);
                std::this_thread::sleep_for(
                    std::chrono::minutes(5));
                continue;
            }
            std::this_thread::sleep_for(retryDelay);
            const auto doubled = retryDelay * 2;
            retryDelay = doubled < std::chrono::seconds(30)
                ? doubled
                : std::chrono::seconds(30);
        }
    }).detach();
}

} // extern "C"

namespace ShadowSecurity {
    bool VerifyLaunchAuth(const ShadowLaunchAuth* auth) noexcept {
        return ShadowSecurity_VerifyLaunchAuth_impl(auth);
    }
    void StartPeriodicSubscriptionCheck() {
        ShadowSecurity_StartPeriodicSubscriptionCheck_impl();
    }
}
