#ifndef ENUMS_H
#define ENUMS_H

enum class LogType {
    NONE,
    MESSAGE,
    WARN,
    ERROR,
    DEBUG
};

enum class Type {
    // Internal types
    INIT,
    ADDCONNECTION,
    REMOVEPEER,
    // Proposal types
    CONFIRMATION,
    REJECT,
    // for group
    CREATE,
    // Commands
    JOIN,
    LEAVE,
    NICK,
    LIST,
    GETTOPIC,
    SETTOPIC,
    MSG,
    QUIT,
    GETMEMBERS,
    NEIGHBORS,
    PING,
    PONG,
    ROUTE,
    PLOT,
    HELP,
    INVALID
};

static Type convertToType(std::string s) {
    for (auto &c: s) c = toupper(c);
    if (s == "JOIN") return Type::JOIN;
    if (s == "LEAVE") return Type::LEAVE;
    if (s == "NICK") return Type::NICK;
    if (s == "LIST") return Type::LIST;
    if (s == "GETTOPIC") return Type::GETTOPIC;
    if (s == "SETTOPIC") return Type::SETTOPIC;
    if (s == "MSG") return Type::MSG;
    if (s == "QUIT") return Type::QUIT;
    if (s == "GETMEMBERS") return Type::GETMEMBERS;
    if (s == "NEIGHBORS") return Type::NEIGHBORS;
    if (s == "PING") return Type::PING;
    if (s == "ROUTE") return Type::ROUTE;
    if (s == "PLOT") return Type::PLOT;
    if (s == "HELP") return Type::HELP;
    return Type::INVALID;
}

#endif
