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

///Create a handle for a file which will be automatically deleted when the 
///handle is dropped. 
///Uses mkstemp internally to produce a unique file name.
///\param nameBase the prefix (which may be a path) for the generated file name.
///                If this is not an absolute path, the file will be created
///                relative to the current working directory. 
FileHandle makeTemporaryFile(const std::string& nameBase);

///Create a handle for a directory which will be automatically deleted when the 
///handle is dropped. 
///Uses mkdtemp internally to produce a unique file name.
///Deletion of the created directory will only work correctly if it empty when
///the handle is dropped. This may be accomplished by creating files within it
///using makeTemporaryFile and ensuring that their handles have shorter 
///lifetimes than that of the directory. 
///\param nameBase the prefix (which may be a path) for the generated directory
///                name. If this is not an absolute path, the directory will be 
///                created relative to the current working directory. 
FileHandle makeTemporaryDir(const std::string& nameBase);

#endif //SLATE_FILEHANDLE_H
