#include <Utilities.h>

#include <cerrno>
#include <cstdlib>
#include <stdexcept>

#include <unistd.h>
#include <sys/stat.h>

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
