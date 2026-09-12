#pragma once

namespace StarfieldHT {

class HookManager {
public:
    static HookManager& Instance();

    bool Initialize();
    bool EnableAllHooks();

    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

private:
    HookManager() = default;
    ~HookManager() = default;

    bool m_initialized = false;
};

} // namespace StarfieldHT
