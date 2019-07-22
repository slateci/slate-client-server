#ifndef SLATE_LOGGING_H
#define SLATE_LOGGING_H

#include <iostream>
#include <sstream>
#include <mutex>
#include <thread>

#include "Utilities.h"
#include "ServerUtilities.h"

struct logTarget{
	std::ostream& stream;
	std::mutex mutex;
	
	void write(const std::string& msg){
		std::lock_guard<std::mutex> guard(mutex);
		stream << msg;
		stream.flush();
	}
};

extern logTarget logStdout;
extern logTarget logStderr;

///Log an informational message to stdout
#define log_info(msg) \
do{ \
	std::ostringstream str; \
	str << "INFO: [" << timestamp() << "] (TID " \
	<< std::this_thread::get_id() << ") " << msg << std::endl; \
	logStdout.write(str.str()); \
} while(0)

///Log that an error or problem has occurred to stderr
#define log_warn(msg) \
do{ \
	std::ostringstream str; \
	str << "WARN: [" << timestamp() << "] (TID " \
	<< std::this_thread::get_id() << ") " << msg << std::endl; \
	logStderr.write(str.str()); \
} while(0)

///Log that an error or problem has occurred to stderr
#define log_error(msg) \
do{ \
	std::ostringstream str; \
	str << "ERROR: [" << timestamp() << "] (TID " \
	<< std::this_thread::get_id() << ") " << msg << std::endl; \
	logStderr.write(str.str()); \
} while(0)

///Log an error to stderr and abort the current activity by throwing an exception
///\throws std::runtime_error
#define log_fatal(msg) \
do{ \
	std::ostringstream mstr; \
	mstr << msg; \
	std::ostringstream str; \
	str << "FATAL: [" << timestamp() << "] (TID " \
	<< std::this_thread::get_id() << ") " << mstr.str() << std::endl; \
	logStderr.write(str.str()); \
	throw std::runtime_error(mstr.str()); \
} while(0)

#endif //SLATE_LOGGING_H
