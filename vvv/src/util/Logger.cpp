#include "vvv/util/Logger.hpp"

// set default values for static
vvv::loglevel vvv::Logger::s_minLevel = DEBUG; ///< only messages with this level or above are printed
bool vvv::Logger::s_printHeader = true;        ///< if a debug level header should be printed before a message
bool vvv::Logger::s_useColors = true;          ///< if messages are colored by their log level with ANSI color codes

// private helper attribute
bool vvv::Logger::s_overwriteLastLine = true;  ///< outputs \r at the begging of this logging line because the last command requested to be overwritten