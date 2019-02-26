#ifndef SLATE_FILEHANDLE_H
#define SLATE_FILEHANDLE_H

#include <memory>
#include <string>

///A RAII object for managing the lifetimes of temporary files
struct FileHandle{
public:
	FileHandle(){}
	///Construct a handle to own the file at the given path
	///\param filePath the path to the file, which should already exist
	///\param isDirectory whether the path is for a directory rather than a 
	///                   regular file
	FileHandle(const std::string& filePath, bool isDirectory=false):
	filePath(filePath),isDirectory(isDirectory){}
	///Destroys the associated file
	~FileHandle();
	///Copying is forbidden
	FileHandle(const FileHandle&)=delete;
	///Move from a handle
	FileHandle(FileHandle&& other):
	filePath(other.filePath),isDirectory(other.isDirectory){
		other.filePath="";
	}
	///Copy assignment is forbidden
	FileHandle& operator=(const FileHandle&)=delete;
	///Move assignment
	FileHandle& operator=(FileHandle&& other){
		if(this!=&other){
			std::swap(filePath,other.filePath);
			std::swap(isDirectory,other.isDirectory);
		}
		return *this;
	}
	///\return the path to the file
	const std::string& path() const{ return filePath; }
	///\return the path to the file
	operator std::string() const{ return filePath; }
private:
	///the path to the owned file
	std::string filePath;
	///whether the file is a directory
	bool isDirectory;
};

///Concatenate a string with the path stored in a file handle
std::string operator+(const char* s, const FileHandle& h);
///Concatenate the path stored in a file handle with a string
std::string operator+(const FileHandle& h, const char* s);

///Wrapper type for sharing ownership of temporay files
using SharedFileHandle=std::shared_ptr<FileHandle>;

FileHandle makeTemporaryFile(const std::string& nameBase);

FileHandle makeTemporaryDir(const std::string& nameBase);

#endif //SLATE_FILEHANDLE_H
