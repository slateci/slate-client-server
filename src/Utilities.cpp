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

std::string getHomeDirectory(){
	std::string path;
	fetchFromEnvironment("HOME",path);
	if(path.empty())
		throw std::runtime_error("Unable to locate home directory");
	if(path.back()!='/')
		path+='/';
	return path;
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

///This is hard to do generally; for now only CSI SGR sequences are identified
///and removed. These are by far the most common sequences in command output, 
///and _appear_ to be the only ones produced by kubectl. 
std::string removeShellEscapeSequences(std::string s){
	std::size_t pos=0;
	while((pos=s.find("\x1B[",pos))!=std::string::npos){
		std::size_t end=s.find('m',pos);
		if(end==std::string::npos)
			s.erase(pos);
		else
			s.erase(pos,end-pos+1);
	}
	return s;
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

int compareVersions(const std::string& a, const std::string& b){
	//1: If the strings are. . .  equal, return 0.
	if(a==b)
		return 0;
	//use xpos as the beginning of the part of string x remaining to be considered
	std::size_t apos=0, bpos=0, aseg, bseg;
	//2: Loop over the strings, left-to-right.
	while(apos<a.size() && bpos<b.size()){
		//2.1: Trim anything that’s not [A-Za-z0-9] or tilde (~) from the front 
		//     of both strings.
		while(apos<a.size() && !(isalnum(a[apos]) || a[apos]=='~'))
			apos++;
		while(bpos<b.size() && !(isalnum(b[bpos]) || b[bpos]=='~'))
			bpos++;
		if(apos==std::string::npos || bpos==std::string::npos)
			break;
		//2.2: If both strings start with a tilde, discard it and move on to the 
		//     next character.
		if(a[apos]=='~' && b[bpos]=='~'){
			apos++;
			bpos++;
			//2.4: End the loop if either string has reached zero length.
			if(apos==a.size() || bpos==b.size())
				break;
		}
		//2.3: If string a starts with a tilde and string b does not, return -1 
		//     (string a is older); and the inverse if string b starts with a 
		//     tilde and string a does not.
		else if(a[apos]=='~')
			return -1;
		else if(b[bpos]=='~')
			return 1;
		//2.5: If the first character of a is a digit, pop the leading chunk of 
		//     continuous digits from each string
		//(define the 'popped' segment as characters [xseg,xpos) for string x)
		if(isdigit(a[apos])){
			aseg=apos++; //increment apos because we know it was a digit
			while(apos<a.size() && isdigit(a[apos]))
				apos++;
			bseg=bpos; //bpos might not be a digit, do not increment
			while(bpos<b.size() && isdigit(b[bpos]))
				bpos++;
		}
		//2.5 cont'd: If a begins with a letter, do the same for leading letters.
		if(isalpha(a[apos])){
			aseg=apos++; //increment apos because we know it was a letter
			while(apos<a.size() && isalpha(a[apos]))
				apos++;
			bseg=bpos; //bpos might not be a letter, do not increment
			while(bpos<b.size() && isalpha(b[bpos]))
				bpos++;
		}
		//2.6: If the segement from b had 0 length, return 1 if the segment from 
		//     a was numeric, or -1 if it was alphabetic. 
		if(bpos==bseg){
			if(isdigit(a[aseg]))
				return 1;
			if(isalpha(a[aseg]))
				return -1;
		}
		//2.7: If the leading segments were both numeric, discard any leading 
		//     zeros. If a is longer than b (without leading zeroes), return 1, 
		//     and vice-versa.
		if(isdigit(a[aseg])){
			while(aseg<apos && a[aseg]=='0') //trim a
				aseg++;
			while(bseg<bpos && b[bseg]=='0') //trim b
				bseg++;
			if(apos-aseg > bpos-bseg) //a is longer than b
				return 1;
			if(bpos-bseg > apos-aseg) //b is longer than a
				return -1;
		}
		//2.8: Compare the leading segments with strcmp(). If that returns a 
		//non-zero value, then return that value.
		//(Implement the equivalent of strcmp inline because segments are 
		//not null terminated.)
		while(aseg<apos && bseg<bpos){
			if(a[aseg]<b[bseg])
				return -1;
			if(a[aseg]>b[bseg])
				return 1;
			aseg++;
			bseg++;
		}
		if(aseg==apos && bseg<bpos)
			return -1;
		if(bseg==bpos && aseg<apos)
			return 1;
	}
	//If the loop ended then the longest wins - if what’s left of a is longer 
	//than what’s left of b, return 1. Vice-versa for if what’s left of b is 
	//longer than what’s left of a. And finally, if what’s left of them is the 
	//same length, return 0.
	if(a.size()-apos > b.size()-bpos)
		return 1;
	if(a.size()-apos < b.size()-bpos)
		return -1;
	return 0;
}
