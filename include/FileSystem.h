#ifndef SLATE_FILESYSTEM_H
#define SLATE_FILESYSTEM_H

#include <string>

#include <sys/types.h>
#include <dirent.h>

// Simple means of iterating over the filesystem
// All default constructed instances are considered equivalent 'end' iterators
struct directory_iterator{
public:
	//a structure for holding filesystem paths which makes inspecting them slightly more convenient
	struct path_t{
	private:
		std::string dirpath;
		std::string itemname;
	public:
		path_t(const std::string& d, const std::string& n):dirpath(d),itemname(n){}
		
		std::string str() const{
			return(dirpath+"/"+itemname);
		}
		std::string name() const{
			return(itemname);
		}
		//extracts the name of the item referred to by this path, without the extension, if any
		//So, the stem of /A/B/file.ext is "file"
		std::string stem() const{
			size_t dotIdx=itemname.rfind('.');
			if(dotIdx==std::string::npos)
				return(itemname);
			return(itemname.substr(0,dotIdx));
		}
		//extracts the extension of the item referred to by this path, if any, omitting the dot
		//So, the extension of /A/B/file.ext is "ext"
		std::string extension() const{
			size_t dotIdx=itemname.rfind('.');
			if(dotIdx==std::string::npos || dotIdx==itemname.size()-1)
				return("");
			return(itemname.substr(dotIdx+1));
		}
		operator std::string() const{ return str(); }
	};
	
	//represents an item found in a directory
	struct directory_entry{
	private:
		const std::string dirPath;
		dirent* item;
		
		friend class directory_iterator;
		friend bool operator==(const directory_iterator& d1, const directory_iterator& d2);
		friend bool is_regular_file(const directory_iterator::directory_entry& entry);
		friend bool is_directory(const directory_iterator::directory_entry& entry);
		friend bool is_symlink(const directory_iterator::directory_entry& entry);
	public:
		directory_entry(const std::string& dp, dirent* i):dirPath(dp),item(i){}
		
		path_t path() const{
			return(path_t(dirPath,item->d_name));
		}
	};
private:
	std::string path;
	DIR* dirp;
	directory_entry curItem;
	
	friend bool operator==(const directory_iterator& d1, const directory_iterator& d2);
public:
	directory_iterator():dirp(NULL),curItem(path,NULL){}
	
	directory_iterator(const std::string& path):
	path(path),dirp(opendir(path.c_str())),
	curItem(this->path,dirp?readdir(dirp):NULL)
	{}
	
	~directory_iterator(){
		if(dirp)
			closedir(dirp);
	}
	const directory_entry& operator*() const{
		return(curItem);
    }
	const directory_entry* operator->() const{
        return(&curItem);
    }
	const directory_entry& operator++(){ //prefix increment
		if(dirp)
			curItem.item=readdir(dirp);
		return(curItem);
	}
	directory_entry operator++ (int){ //postfix increment
		directory_entry temp(curItem);
		if(dirp)
			curItem.item=readdir(dirp);
		return(temp);
	}
	explicit operator bool() const{
		return !!dirp;
	}
	bool operator!() const{
		return !dirp;
	}
};

///An object representing a directory, whose contents can be iterated over
struct directory{
public:
	directory(std::string path):path(path){}
	directory_iterator begin() const{ return directory_iterator(path); }
	directory_iterator end() const{ return directory_iterator(); }
private:
	std::string path;
};

bool operator==(const directory_iterator& d1, const directory_iterator& d2);
bool operator!=(const directory_iterator& d1, const directory_iterator& d2);
bool is_regular_file(const directory_iterator::directory_entry& entry);
bool is_directory(const directory_iterator::directory_entry& entry);
bool is_directory(const std::string& path);
bool is_symlink(const directory_iterator::directory_entry& entry);

///\throws std::runtime_error on failure
void mkdir_p(const std::string& path, uint16_t mode);

///\return 0 on success, otherwise an error constant
int recursivelyDestroyDirectory(const std::string& path);

#endif //SLATE_FILESYSTEM_H
