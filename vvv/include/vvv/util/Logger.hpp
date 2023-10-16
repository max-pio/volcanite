#pragma once

/*
 * Logger base taken from
 * https://stackoverflow.com/questions/5028302/small-logger-class
 * original author: Alberto Lepe <dev@alepe.com>, December 1, 2015, 6:00 PM
 */

#include <chrono>
#include <cassert>
#include <sstream>
#include <iostream>

namespace vvv {

template<typename T>
std::string arrayToString(const T* data, size_t count, const std::string& delimiter=",") {
    assert(count <= 1024 && "won't concat more than 1024 array elements into one string");
    std::stringstream ss("");
    for (int i = 0; i < count; i++) {
        ss << data[i];
        if (i < count-1)
            ss << delimiter;
    }
    return ss.str();
}



enum loglevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    Logger() {
        m_msglevel = INFO;
        m_out = &std::cout;
        m_overwriteThisLine = false;
        if (s_printHeader) {
            operator<<("[" + getLabel(m_msglevel) + "]");
        }
    }

#ifdef _WIN64
    // todo: not a great fix to work around the ERROR = 0 define in wingdi.h (windows.h)
    explicit Logger(int level, bool overwriteWithNextLine = false) {
        if(level == 0)
            m_msglevel = ERROR;
        else
            m_msglevel = INFO;
        if (m_msglevel > WARN)
            m_out = &std::cerr;
        else
            m_out = &std::cout;

        if(s_overwriteLastLine) {
            operator<<("\r");
            s_overwriteLastLine = false;
        }
        m_overwriteThisLine = overwriteWithNextLine;

        if (s_printHeader) {
            operator<<(getLabel(m_msglevel));
        }
    }
#endif

    explicit Logger(loglevel type, bool overwriteWithNextLine = false) {
        m_msglevel = type;
        if (m_msglevel > WARN)
            m_out = &std::cerr;
        else
            m_out = &std::cout;

        if(s_overwriteLastLine) {
            operator<<("\r");
            s_overwriteLastLine = false;
        }
        m_overwriteThisLine = overwriteWithNextLine;

        if (s_printHeader) {
            operator<<(getLabel(type));
        }
    }
    ~Logger() {
        if (m_opened) {
            if (s_useColors)
                *m_out << "\033[0m";
            if(!m_overwriteThisLine) {
                *m_out << std::endl;
            }
            else {
                m_out->flush();
                s_overwriteLastLine = true;
            }
        }
        m_opened = false;
    }
    template <class T> Logger &operator<<(const T &msg) {
        if (m_msglevel >= s_minLevel) {
            *m_out << msg;
            m_opened = true;
        }
        return *this;
    }

    static bool getUseColors() {
        return s_useColors;
    }

    static loglevel s_minLevel;

private:
    bool m_opened = false;
    bool m_overwriteThisLine = false;
    loglevel m_msglevel = DEBUG;
    std::ostream *m_out = &std::cout;

    static bool s_overwriteLastLine;

    static inline std::string getLabel(loglevel type) {
        std::string label;
        if (s_useColors) {
            switch (type) {
            case DEBUG:
                label = "\033[32m[DEBUG] ";
                break;
            case INFO:
                label = "\033[0m[INFO]  ";
                break;
            case WARN:
                label = "\033[33m[WARN]  ";
                break;
            case ERROR:
                label = "\033[31m[ERROR] ";
                break;
            }
            return label;
        } else {
            switch (type) {
            case DEBUG:
                label = "[DEBUG] ";
                break;
            case INFO:
                label = "[INFO]  ";
                break;
            case WARN:
                label = "[WARN]  ";
                break;
            case ERROR:
                label = "[ERROR] ";
                break;
            }
            return label;
        }
    }

    static bool s_printHeader;
    static bool s_useColors;
};

}
