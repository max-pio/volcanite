#include "vvv/util/Logger.hpp"

// set default values for static
vvv::loglevel vvv::Logger::s_minLevel = DEBUG; ///< only messages with this level or above are printed
bool vvv::Logger::s_printHeader = true;        ///< if a debug level header should be printed before a message
#ifndef __WIN32
bool vvv::Logger::s_useColors = true;          ///< if messages are colored by their log level with ANSI color codes
#else
bool vvv::Logger::s_useColors = false;          ///< if messages are colored by their log level with ANSI color codes
#endif

// private helper attribute
bool vvv::Logger::s_overwriteLastLine = true;  ///< outputs \r at the begging of this logging line because the last command requested to be overwritten