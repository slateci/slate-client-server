#include <FileSystem.h>

#include <cerrno>
#include <stdexcept>

#include <unistd.h>
#include <sys/stat.h>

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

bool is_regular_file(const directory_iterator::directory_entry& entry){
	if(!entry.item)
		return(false);
	return(entry.item->d_type==DT_REG);
}

bool is_directory(const directory_iterator::directory_entry& entry){
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

bool is_symlink(const directory_iterator::directory_entry& entry){
	if(!entry.item)
		return(false);
	return(entry.item->d_type==DT_LNK);
}

void mkdir_p(const std::string& path, uint16_t mode){
	auto makeIfNonexistent=[=](const std::string& path){
		bool make=false;
		struct stat data;
		int err=stat(path.c_str(),&data);
		if(err!=0){
			err=errno;
			if(err!=ENOENT)
				throw std::runtime_error("Unable to stat "+path);
			else
				make=true;
		}
		
		if(make){
			err=mkdir(path.c_str(),mode);
			if(err){
				err=errno;
				throw std::runtime_error("Unable to create directory "+path+": error "+std::to_string(err));
			}
		}
	};

	if(path.empty())
		throw std::logic_error("The empty path is not a valid argument to mkdir");
	if(mode>0777)
		throw std::logic_error("mkdir does not permit setting any mode bits above the lowest 9");
	size_t slashPos=path.find('/',1); //if the first character is a slash we don't care
	while(slashPos!=std::string::npos){
		std::string parentPath=path.substr(0,slashPos);
		makeIfNonexistent(parentPath);
		slashPos=path.find('/',slashPos+1);
	}
	if(path.back()!='/') //only necessary if the loop didn't already get everything
		makeIfNonexistent(path);
}

int recursivelyDestroyDirectory(const std::string& path){
	directory_iterator it(path);
	if(!it)
		return ENOTDIR; //may not be right in all cases
	for(const directory_iterator end; it!=end; it++){
		if(it->path().name()=="." || it->path().name()=="..")
			continue;
		if(is_directory(*it)){
			int err=recursivelyDestroyDirectory(it->path().str());
			if(err)
				return err;
			err=rmdir(it->path().str().c_str());
			if(err!=0){
				err=errno;
				return err;
			}
		}
		else{
			int err=unlink(it->path().str().c_str());
			if(err!=0){
				err=errno;
				return err;
			}
		}
	}
	return 0;
}
