#ifndef SLATE_UTILITIES_H
#define SLATE_UTILITIES_H

#include "crow.h"

#include "Entities.h"
#include "PersistentStore.h"

///\param store the database in which to look up the user
///\param token the proffered authentication token. May be NULL if missing.
const User authenticateUser(PersistentStore& store, const char* token);

///Construct a JSON error object
///\param message the explanation to include in the error
///\return a JSON object with a 'kind' of "Error"
crow::json::wvalue generateError(const std::string& message);

///Run a shell command
///\warning This function executes the given string in the shell, so it _must_
///         be sanitized to avoid arbitrary code execution by users
///\param the command, including arguments, to be run
///\return all data written to standard output by the child process
std::string runCommand(const std::string& command);

template<typename ContainerType, 
         typename KeyType=typename ContainerType::key_type,
         typename MappedType=typename ContainerType::mapped_type>
const MappedType& findOrDefault(const ContainerType& container, 
                                const KeyType& key, const MappedType& def){
	auto it=container.find(key);
	if(it==container.end())
		return def;
	return it->second;
}

template<typename ContainerType, 
         typename KeyType=typename ContainerType::key_type,
         typename MappedType=typename ContainerType::mapped_type>
const MappedType& findOrThrow(const ContainerType& container, 
                              const KeyType& key, const std::string& err){
	auto it=container.find(key);
	if(it==container.end())
		throw std::runtime_error(err);
	return it->second;
}

///Construct a compacted YAML string with whitespace only lines and comments
///removed
std::string reduceYAML(const std::string& input);

#endif //SLATE_UTILITIES_H
