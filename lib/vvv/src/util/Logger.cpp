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

#include "vvv/util/Logger.hpp"

// set default values for static
vvv::loglevel vvv::Logger::s_minLevel = Debug; ///< only messages with this level or above are printed
bool vvv::Logger::s_printHeader = true;        ///< if a debug level header should be printed before a message
#ifndef _WIN32
bool vvv::Logger::s_useColors = true; ///< if messages are colored by their log level with ANSI color codes
#else
bool vvv::Logger::s_useColors = false; ///< if messages are colored by their log level with ANSI color codes
#endif

// private helper attribute
bool vvv::Logger::s_overwriteLastLine = true; ///< outputs \r at the begging of this logging line because the last command requested to be overwritten