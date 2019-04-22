#ifndef SLATE_UTILITES_H
#define SLATE_UTILITES_H

#include <cstdlib>
#include <string>

///Try to get the value of an enviroment variable and store it to a string object.
///If the variable was not set \p target will not be modified. 
///\tparam a type to which a C-string can be assigned
///\param name the name of the environment variable to get
///\param target the variable into which the environment variable should be 
///              copied, if set
///\return whether the environment variable was set
template <typename TargetType>
bool fetchFromEnvironment(const std::string& name, TargetType& target){
	char* val=getenv(name.c_str());
	if(val){
		target=val;
		return true;
	}
	return false;
}

///Get the path to the user's home directory
///\return home directory path, with a trailing slash
///\throws std::runtime_error if $HOME is not set to a non-empty string
std::string getHomeDirectory();

enum class PermState{
	VALID,
	INVALID,
	DOES_NOT_EXIST //This really is FILE_NOT_FOUND
};

///Ensure that the given path is readable only by the owner
PermState checkPermissions(const std::string& path);

///Get the filesystem path for the main executable
std::string program_location();

#endif
