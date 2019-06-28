#ifndef SLATE_LOGGING_H
#define SLATE_LOGGING_H

#include <iostream>
#include <sstream>

#include "Utilities.h"
#include "ServerUtilities.h"

///Log an informational message to stdout
#define log_info(msg) \
do{ std::cout << "INFO: [" << timestamp() << "] " << msg << std::endl; }while(0)

///Log that an error or problem has occurred to stderr
#define log_warn(msg) \
do{ std::cerr << "WARNING: [" << timestamp() << "] " << msg << std::endl; }while(0)

///Log that an error or problem has occurred to stderr
#define log_error(msg) \
do{ std::cerr << "ERROR: [" << timestamp() << "] " << msg << std::endl; }while(0)

///Log an error to stderr and abort the current activity by throwing an exception
///\throws std::runtime_error
#define log_fatal(msg) \
do{ \
	std::ostringstream str; \
	str << msg; \
	std::cerr << "FATAL: [" << timestamp() << "] " << str.str() << std::endl; \
	throw std::runtime_error(str.str()); \
}while(0)

#endif //SLATE_LOGGING_H
