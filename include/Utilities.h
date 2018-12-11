#ifndef SLATE_UTILITES_H
#define SLATE_UTILITES_H

#include <string>

bool fetchFromEnvironment(const std::string& name, std::string& target);

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