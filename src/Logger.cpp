#include <thread>
#include "Logger.h"

#pragma region Constructor

Logger::Logger() = default;

Logger &Logger::getInstance() {
    // https://stackoverflow.com/questions/43523509/simple-singleton-example-in-c
    static Logger instance;
    return instance;
}

#pragma endregion

/**
 * Shows if the Logger has messages to output.
 * @return true = has messages
 */
bool Logger::hasOutput() {
    return !messageQueue.empty();
}

/**
 * Pop the first message from the outputMessageQueue.
 * @return The popped message
 */
std::string Logger::popOutputMessage() {
    auto message = messageQueue.front();
    messageQueue.pop();
    return message;
}

/**
 * Add a string to the logger with time and type prefix.
 * @param message
 * @param type default is SYSTEM
 */
void Logger::log(const std::string &message, const LogType type) {
    if (!debug && type == LogType::DEBUG) return;

    time_t now = time(nullptr);
    struct tm time{};
    char buf[80];
    time = *localtime(&now);
    strftime(buf, sizeof(buf), "%X", &time);

    std::string prefix;
    switch (type) {
        case LogType::WARN:
            prefix = "Warning: ";
            break;
        case LogType::ERROR:
            prefix = "Error: ";
            break;
        case LogType::DEBUG:
            prefix = "Debug: ";
            break;
        case LogType::MESSAGE:
            prefix = "Message: ";
            break;
        case LogType::NONE:
            break;
    }

    messageQueue.push("[" + std::string(buf) + "] " + prefix + message);
}

/**
 * Prevent the Client from exiting while the logger has output left.
 * @param status Exit status
 */
void Logger::outputExit(int status) {
    while (hasOutput()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    exit(status);
}