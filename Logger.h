#pragma once
#include <string>
#include <mutex>
#include <fstream>

enum class LogLevel { INFO, DEBUG, ERR };

class Logger {
public:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void setEnabled(bool enabled);
    void setLevel(LogLevel level);
    void log(const std::string& msg, LogLevel level);

private:
    bool       m_enabled = true;
    LogLevel   m_level   = LogLevel::INFO;
    mutable std::mutex m_mutex;
    std::ofstream      m_file;
};
