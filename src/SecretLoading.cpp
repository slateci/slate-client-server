#include <SecretLoading.h>

#include <cerrno>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "CLI11.hpp"

namespace{
struct directory_iterator; //fwd decl

//a structure for holding filesystem paths which makes inspecting them slightly more convenient
struct fsPath{
private:
	std::string dirpath;
	std::string itemname;
public:
	fsPath(const std::string& d, const std::string& n):dirpath(d),itemname(n){}
	
	std::string string() const{
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
	operator std::string() const{ return string(); }
};

//represents an item found in a directory
struct directory_entry{
private:
	const std::string dirPath;
	dirent* item;
	
	friend class directory_iterator;
	friend bool operator==(const directory_iterator& d1, const directory_iterator& d2);
	friend bool is_directory(const directory_entry& entry);
	friend bool is_regular_file(const directory_entry& entry);
public:
	directory_entry(const std::string& dp, dirent* i):dirPath(dp),item(i){}
	
	fsPath path() const{
		return(fsPath(dirPath,item->d_name));
	}
};

// Simple means of iterating over the filesystem
// All default constructed instances are considered equivalent 'end' iterators
struct directory_iterator{
private:
	std::string path;
	DIR* dirp;
	directory_entry curItem;
	
	friend bool operator==(const directory_iterator& d1, const directory_iterator& d2);
	
	static DIR* opendir(const std::string& path){
		DIR* dir=::opendir(path.c_str());
		if(!dir){
			int err=errno;
			throw std::runtime_error("Unable to open directory "+path+": Error "+std::to_string(err));
		}
		return dir;
	}
public:
	directory_iterator():dirp(NULL),curItem(path,NULL){}
	
	directory_iterator(const std::string& path):
	path(path),dirp(opendir(path)),
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
};

bool operator==(const directory_iterator& d1, const directory_iterator& d2);
bool operator!=(const directory_iterator& d1, const directory_iterator& d2);
bool is_directory(const directory_entry& entry);

bool operator==(const directory_iterator& d1, const directory_iterator& d2){
	if(d1.dirp==NULL && d2.dirp==NULL)
		return(true);
	if(!!d1.curItem.item != !!d2.curItem.item)
		return(false);
	if(d1.curItem.item==NULL && d2.curItem.item==NULL)
		return(true);
	if(d1.curItem.item->d_ino!=d2.curItem.item->d_ino)
		return(false);
	char* n1=d1.curItem.item->d_name,* n2=d2.curItem.item->d_name;
	for(int i=0; *n1 && *n2; i++)
		if(*n1++!=*n2++)
			return(false);
	return(true);
}

bool operator!=(const directory_iterator& d1, const directory_iterator& d2){
	return(!operator==(d1,d2));
}

//Unfortunately, dirent::d_type is apparently a BSD extension, 
//so to be truly portable we should not use it. However, it is implemented in
//glibc/on Linux and is naturally available on Mac OS, so this isn't the worst
//thing possible. 

bool is_directory(const directory_entry& entry){
	if(!entry.item)
		return(false);
	return(entry.item->d_type==DT_DIR);
}

bool is_directory(const std::string& path){
	struct stat results;
	int err=stat(path.c_str(), &results);
	if(err){
		err=errno;
		throw std::runtime_error("Unable to stat "+path);
	}
	return results.st_mode & S_IFDIR;
}

struct directory{
public:
	directory(std::string path):path(path){}
	directory_iterator begin() const{ return directory_iterator(path); }
	directory_iterator end() const{ return directory_iterator(); }
private:
	std::string path;
};

// end of filesystem infrastructure

std::string getFileContents(const std::string& path){
	std::string contents;
	std::ifstream infile(path);
	if(!infile)
		throw CLI::ValidationError("Unable to open "+path+" for reading");
	std::istreambuf_iterator<char> iit(infile), end;
	std::copy(iit,end,std::back_inserter(contents));
	return contents;
}
	
std::string basename(const std::string& path){
	size_t slashIdx=path.rfind('/');
		if(slashIdx==std::string::npos)
			return path;
		if(slashIdx==path.size()-1)
			return("");
		return(path.substr(slashIdx+1));
}
	
} //anonymous namespace

bool validKey(const std::string& key){
	if(key.empty() || key.size()>253)
		return false;
	const static std::string allowedKeyCharacters="-._0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if(key.find_first_not_of(allowedKeyCharacters)!=std::string::npos)
		return false;
	return true;
}

void parseFromFileSecretEntry(const std::string& arg, std::vector<std::string>& output){
	if(arg.empty())
		throw CLI::ValidationError("argument to --from-file must not be empty");
	try{
		if(is_directory(arg))
			return parseFromDirectorySecretEntry(arg,output);
	}catch(std::runtime_error&){
		//if stat failed then it wasn't a directory anyway
	}
	std::string inputPath, key;
	std::size_t equalPos=arg.find('=');
	key=arg.substr(0,equalPos);
	if(equalPos!=std::string::npos){
		if(equalPos==arg.size()-1)
			throw CLI::ValidationError("non-empty file path must follow equals in argument to --from-file");
		inputPath=arg.substr(equalPos+1);
	}
	else{
		inputPath=key;
		key=basename(key);
	}
	
	std::string value=getFileContents(inputPath);
	output.push_back(key+'='+value);
}

void parseFromDirectorySecretEntry(const std::string& arg, std::vector<std::string>& output){
	for(const auto& entry : directory(arg)){
		if(!is_directory(entry) && validKey(entry.path().name()))
			output.push_back(entry.path().name()+'='+getFileContents(entry.path()));
	}
}

void parseFromEnvFileSecretEntry(const std::string& arg, std::vector<std::string>& output){
	if(arg.empty())
		throw CLI::ValidationError("argument to --from-env-file must not be empty");
	
	std::ifstream infile(arg);
	if(!infile)
		throw CLI::ValidationError("Unable to open "+arg+" for reading");
	std::string line;
	std::size_t lineNum=1;
	while(std::getline(infile,line)){
		std::size_t equalPos=line.find('=');
		if(equalPos==std::string::npos)
			throw CLI::ValidationError("No key=value pair on line "+
			                           std::to_string(lineNum)+" of "+arg);
		std::string key=line.substr(0,equalPos);
		std::string value=line.substr(equalPos+1);
		if(!validKey(key))
			throw CLI::ValidationError("Invalid key on line "+
			                           std::to_string(lineNum)+" of "+arg);
		output.push_back(key+'='+value);
		lineNum++;
	}
}