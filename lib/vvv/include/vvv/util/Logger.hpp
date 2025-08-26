//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

/*
 * Logger base taken from
 * https://stackoverflow.com/questions/5028302/small-logger-class
 * original author: Alberto Lepe <dev@alepe.com>, December 1, 2015, 6:00 PM
 */

#include <chrono>
#include <iostream>
#include <sstream>

namespace vvv {

template <typename T>
std::string arrayToString(const T *data, size_t count, const std::string &delimiter = ",") {
    std::stringstream ss("");
    bool dots = count > 1024;
    if (dots)
        count = 1024;
    for (int i = 0; i < count; i++) {
        ss << data[i];
        if (i < count - 1)
            ss << delimiter;
    }
    if (dots)
        ss << "...";
    return ss.str();
}

enum loglevel { Debug,
                Info,
                Warn,
                Error };

class Logger {
  public:
    Logger() {
        m_msglevel = Info;
        m_out = &std::cout;
        m_overwriteThisLine = false;
        if (s_printHeader) {
            operator<<("[" + getLabel(m_msglevel) + "]");
        }
    }

    explicit Logger(loglevel type, bool overwriteWithNextLine = false) {
        m_msglevel = type;
        if (m_msglevel > Warn)
            m_out = &std::cerr;
        else
            m_out = &std::cout;

        if (s_overwriteLastLine) {
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
            if (!m_overwriteThisLine) {
                *m_out << std::endl;
            } else {
                m_out->flush();
                s_overwriteLastLine = true;
            }
        }
        m_opened = false;
    }
    template <class T>
    Logger &operator<<(const T &msg) {
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
    loglevel m_msglevel = Debug;
    std::ostream *m_out = &std::cout;

    static bool s_overwriteLastLine;

    static inline std::string getLabel(loglevel type) {
        std::string label;
        if (s_useColors) {
            switch (type) {
            case Debug:
                label = "\033[32m[DEBUG] ";
                break;
            case Info:
                label = "\033[0m[INFO]  ";
                break;
            case Warn:
                label = "\033[33m[WARN]  ";
                break;
            case Error:
                label = "\033[31m[ERROR] ";
                break;
            }
            return label;
        } else {
            switch (type) {
            case Debug:
                label = "[DEBUG] ";
                break;
            case Info:
                label = "[INFO]  ";
                break;
            case Warn:
                label = "[WARN]  ";
                break;
            case Error:
                label = "[ERROR] ";
                break;
            }
            return label;
        }
    }

    static bool s_printHeader;
    static bool s_useColors;
};

} // namespace vvv
