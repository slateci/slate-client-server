#include <FileHandle.h>

#include <cerrno>

#include <unistd.h>

#include <Logging.h>

FileHandle::~FileHandle(){
	if(!filePath.empty()){
		if(!isDirectory){ //regular file
			int err=remove(filePath.c_str());
			if(err!=0){
				err=errno;
				log_error("Failed to remove file " << filePath << " errno: " << err);
			}
		}
		else{ //directory
			int err=rmdir(filePath.c_str());
			if(err!=0){
				err=errno;
				log_error("Failed to remove directory " << filePath << " errno: " << err);
			}
		}
	}
}

std::string operator+(const char* s, const FileHandle& h){
	return s+h.path();
}
std::string operator+(const FileHandle& h, const char* s){
	return h.path()+s;
}

FileHandle makeTemporaryFile(const std::string& nameBase){
	std::string base=nameBase+"XXXXXXXX";
	//make a modifiable copy for mkstemp to scribble over
	std::unique_ptr<char[]> filePath(new char[base.size()+1]);
	strcpy(filePath.get(),base.c_str());
	struct fdHolder{
		int fd;
		~fdHolder(){ close(fd); }
	} fd{mkstemp(filePath.get())};
	if(fd.fd==-1){
		int err=errno;
		log_fatal("Creating temporary file failed with error " << err);
	}
	return FileHandle(filePath.get());
}