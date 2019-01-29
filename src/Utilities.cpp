#include <Utilities.h>

#include <cerrno>
#include <cstdlib>
#include <memory>
#include <stdexcept>

#include <unistd.h>
#include <sys/stat.h>

//needed to select correct implementation of program_location
#include "OSDetection.h"

bool fetchFromEnvironment(const std::string& name, std::string& target){
	char* val=getenv(name.c_str());
	if(val){
		target=val;
		return true;
	}
	return false;
}

PermState checkPermissions(const std::string& path){
	struct stat data;
	int err=stat(path.c_str(),&data);
	if(err!=0){
		err=errno;
		if(err==ENOENT)
			return PermState::DOES_NOT_EXIST;
		//TODO: more detail on what failed?
		throw std::runtime_error("Unable to stat "+path);
	}
	//check that the current user is actually the file's owner
	if(data.st_uid!=getuid())
		return PermState::INVALID;
	return((data.st_mode&((1<<9)-1))==0600 ? PermState::VALID : PermState::INVALID);
}

//Implementation of program_location based on 
//boost::dll::detail::program_location_impl for POSIX, with changes to avoid 
//having to link against libboost_system or libboost_filesystem

#if BOOST_OS_MACOS || BOOST_OS_IOS

#include <mach-o/dyld.h>

std::string program_location(){
	char path[1024];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size) == 0)
		return path;
	
	std::unique_ptr<char[]> p(new char[size]);
	if (_NSGetExecutablePath(p.get(), &size) != 0)
		throw std::runtime_error("Error getting executable path");
	
	std::string ret(p.get());
	return ret;
}

#elif BOOST_OS_SOLARIS

#include <stdlib.h>

std::string program_location(){
	return getexecname();
}

#elif BOOST_OS_BSD_FREE

#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdlib.h>

std::string program_location(){
	int mib[4];
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1;
	size_t cb=0;
	int res=sysctl(mib, 4, NULL, &cb, NULL, 0);
	if(res!=0){
		res=errno;
		if(res!=ENOMEM)
			throw std::runtime_error("Error getting executable path");
	}
	std::unique_ptr<char[]> p(new char[cb]);
	res=sysctl(mib, 4, p.get(), &cb, NULL, 0);
	if(res!=0)
		throw std::runtime_error("Error getting executable path");
	
	std::string ret(p.get());
	return ret;
}

#elif BOOST_OS_BSD_NET

#include <unistd.h>

std::string program_location(){
	const static size_t bufsize=10240;
	std::unique_ptr<char[]> buf(new char[bufsize]);
	ssize_t res readlink("/proc/curproc/exe", buf.get(), bufsize-1);
	if(res==-1)
		throw std::runtime_error("Error getting executable path");
	buf[res]='\0';
	std::string ret(buf.get());
	return ret;
}

#elif BOOST_OS_BSD_DRAGONFLY

std::string program_location(){
	const static size_t bufsize=10240;
	std::unique_ptr<char[]> buf(new char[bufsize]);
	ssize_t res readlink("/proc/curproc/file", buf.get(), bufsize-1);
	if(res==-1)
		throw std::runtime_error("Error getting executable path");
	buf[res]='\0';
	std::string ret(buf.get());
	return ret;
}

#elif BOOST_OS_QNX

#include <fstream>
#include <string> // for std::getline

std::string program_location(){
	std::string s;
	std::ifstream ifs("/proc/self/exefile");
	std::getline(ifs, s);
	
	if (ifs.fail() || s.empty()) 
		throw std::runtime_error("Error getting executable path");
	
	return s;
}

#else  // BOOST_OS_LINUX || BOOST_OS_UNIX || BOOST_OS_HPUX || BOOST_OS_ANDROID

std::string program_location(){
	const static size_t bufsize=10240;
	std::unique_ptr<char[]> buf(new char[bufsize]);
	ssize_t res=readlink("/proc/self/exe", buf.get(), bufsize-1);
	if(res==-1)
		throw std::runtime_error("Error getting executable path");
	buf[res]='\0';
	std::string ret(buf.get());
	return ret;
}

#endif
