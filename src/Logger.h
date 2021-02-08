#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <queue>
#include "Enums.h"

class Logger {
public:
    static Logger& getInstance();

    // methods
    bool hasOutput();
    std::string popOutputMessage();
    void log(const std::string& message, LogType type = LogType::NONE);
    void outputExit(int status);

    // setter
    void setDebug(bool value) { debug = value; }
private:
    // private constructor
    Logger();

    // fields
    std::queue<std::string> messageQueue;
    bool debug = false;
};

#endif
