#include "pch.h"
#include "hook_manager.h"
#include "core/logger.h"

namespace StarfieldHT {

HookManager& HookManager::Instance() {
    static HookManager instance;
    return instance;
}

bool HookManager::Initialize() {
    if (m_initialized) {
        return true;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        Logger::Instance().Error("MH_Initialize failed: %d", static_cast<int>(status));
        return false;
    }

    m_initialized = true;
    Logger::Instance().Info("MinHook initialized successfully");
    return true;
}

bool HookManager::EnableAllHooks() {
    if (!m_initialized) {
        return false;
    }

    MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        Logger::Instance().Error("MH_EnableHook(MH_ALL_HOOKS) failed: %d", static_cast<int>(status));
        return false;
    }

    return true;
}

} // namespace StarfieldHT
