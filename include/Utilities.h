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

///Remove ANSI escape sequences from a string. 
std::string removeShellEscapeSequences(std::string s);

///Get the filesystem path for the main executable
std::string program_location();

///Compare version strings in the same manner as rpmvercmp, as described by
///https://blog.jasonantman.com/2014/07/how-yum-and-rpm-compare-versions/#how-rpm-compares-version-parts
///retrieved 20190702
///\return -1 if a represents an older version than b
///         0 if a and b represent the same version
///         1 if a represents a newer version than b
int compareVersions(const std::string& a, const std::string& b);

#endif
