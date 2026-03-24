#include "Logger.h"
#include <ctime>

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open())
        m_file.close();
}

void Logger::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = level;
}

void Logger::log(const std::string& msg, LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_enabled || static_cast<int>(level) > static_cast<int>(m_level))
        return;

    if (!m_file.is_open())
        m_file.open("frapsLog.txt", std::ios::out | std::ios::app);

    if (!m_file.is_open())
        return;

    std::time_t t = std::time(nullptr);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    const char* lvlStr = (level == LogLevel::ERR)   ? "ERROR"
                       : (level == LogLevel::DEBUG) ? "DEBUG"
                                                     : "INFO";

    m_file << "[" << timeBuf << "] [" << lvlStr << "] " << msg << "\n";
    m_file.flush();
}
