#include <FileHandle.h>

#include <cerrno>

#include <unistd.h>

FileHandle::~FileHandle(){
	if(!filePath.empty()){
		if(!isDirectory){ //regular file
			int err=remove(filePath.c_str());
			if(err!=0){
				err=errno;
				throw std::runtime_error("Failed to remove file "+filePath+" errno: "+std::to_string(err));
			}
		}
		else{ //directory
			int err=rmdir(filePath.c_str());
			if(err!=0){
				err=errno;
				throw std::runtime_error("Failed to remove directory "+filePath+" errno: "+std::to_string(err));
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
		throw std::runtime_error("Creating temporary file failed with error "+std::to_string(err));
	}
	return FileHandle(filePath.get());
}

FileHandle makeTemporaryDir(const std::string& nameBase){
	const std::string base=nameBase+"XXXXXXXX";
	//make a modifiable copy for mkdtemp to scribble over
	std::unique_ptr<char[]> tmpl(new char[base.size()+1]);
	strcpy(tmpl.get(),base.c_str());
	char* dirPath=mkdtemp(tmpl.get());
	if(!dirPath){
		int err=errno;
		throw std::runtime_error("Creating temporary directory failed with error "+std::to_string(err));
	}
	return FileHandle(dirPath,true);
}