#include "pch.h"
#include "logger.h"
#include "path_utils.h"
#include <cstdio>

namespace StarfieldHT {

namespace {

// One formatted line, and the timestamp prefix it carries.
constexpr size_t kMaxMessageLength   = 2048;
constexpr size_t kMaxTimestampLength = 32;

const char* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

// Untrusted strings reach the log (game-supplied Scaleform method names, for
// one), and an embedded newline or escape sequence would otherwise inject a
// fake line or corrupt the layout. Tabs are the one control character kept.
void StripControlCharacters(char* text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\t') continue;
        if (c < 0x20 || c == 0x7F) text[i] = ' ';
    }
}

} // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

bool Logger::Initialize() {
    DWORD rotateError = 0;
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_initialized) {
        return true;
    }

    std::string logPath = GetModulePath("HeadTracking.log");
    if (logPath.empty()) {
        return false;
    }
    // One generation is kept because a hook fault ends the session, and the
    // user relaunches the game before they get round to sending the log.
    std::string prevPath = GetModulePath("HeadTracking.prev.log");
    if (!MoveFileExA(logPath.c_str(), prevPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        rotateError = GetLastError();
    }

    m_logFile.open(logPath, std::ios::out | std::ios::trunc);
    if (!m_logFile.is_open()) {
        return false;
    }

#ifdef _DEBUG
    if (AllocConsole()) {
        FILE* pFile = nullptr;
        freopen_s(&pFile, "CONOUT$", "w", stdout);
        freopen_s(&pFile, "CONOUT$", "w", stderr);
        m_consoleAllocated = true;
        SetConsoleTitleA("Starfield Head Tracking Debug Console");
    }
    m_minLevel = LogLevel::Debug;
#endif

    m_initialized = true;
    lock.unlock();

    // Reported only once the log is open, and only for a real failure: the
    // truncating open above has already destroyed the generation the rotation
    // existed to keep, so a silent failure loses the previous session.
    if (rotateError != 0 && rotateError != ERROR_FILE_NOT_FOUND) {
        Warning("Could not rotate HeadTracking.log to HeadTracking.prev.log (error %lu) - "
                "the previous session's log was overwritten", rotateError);
    }
    return true;
}

void Logger::LogVa(LogLevel level, const char* fmt, va_list args) {
    char buffer[kMaxMessageLength];
    const int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    if (written < 0) {
        WriteLog(level, "<log format error>");
        return;
    }
    // vsnprintf reports what it WOULD have written, so a truncated message
    // reports more than the buffer holds.
    const size_t length = (static_cast<size_t>(written) < sizeof(buffer))
                              ? static_cast<size_t>(written) : (sizeof(buffer) - 1);
    StripControlCharacters(buffer, length);
    WriteLog(level, buffer);
}

void Logger::Debug(const char* fmt, ...) {
    if (LogLevel::Debug < m_minLevel) return;
    va_list args;
    va_start(args, fmt);
    LogVa(LogLevel::Debug, fmt, args);
    va_end(args);
}

void Logger::Info(const char* fmt, ...) {
    if (LogLevel::Info < m_minLevel) return;
    va_list args;
    va_start(args, fmt);
    LogVa(LogLevel::Info, fmt, args);
    va_end(args);
}

void Logger::Warning(const char* fmt, ...) {
    if (LogLevel::Warning < m_minLevel) return;
    va_list args;
    va_start(args, fmt);
    LogVa(LogLevel::Warning, fmt, args);
    va_end(args);
}

void Logger::Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogVa(LogLevel::Error, fmt, args);
    va_end(args);
}

void Logger::WriteLog(LogLevel level, const char* message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_s(&tm, &time);

    char timestamp[kMaxTimestampLength];
    snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d.%03d",
             tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));

    const char* const levelStr = LevelToString(level);

    if (m_logFile.is_open()) {
        m_logFile << "[" << timestamp << "] [" << levelStr << "] " << message << std::endl;
    }

#ifdef _DEBUG
    if (m_consoleAllocated) {
        printf("[%s] [%s] %s\n", timestamp, levelStr, message);
    }
#endif
}

} // namespace StarfieldHT
