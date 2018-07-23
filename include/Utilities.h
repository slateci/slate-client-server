#ifndef SLATE_UTILITIES_H
#define SLATE_UTILITIES_H

#include "crow.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <sstream>
#include "Entities.h"
#include "PersistentStore.h"

///\return a timestamp rendered as a string with format "YYYY-mmm-DD HH:MM:SS UTC"
std::string timestamp();

///\param store the database in which to look up the user
///\param token the proffered authentication token. May be NULL if missing.
const User authenticateUser(PersistentStore& store, const char* token);

///Construct a JSON error object
///\param message the explanation to include in the error
///\return a JSON object with a 'kind' of "Error"
std::string generateError(const std::string& message);

///Replace characters considered invalid by RapidJSON (such as \n, \t, etc) with accepted character
///\param message the string to replace invalid characters in
///\return a string with replaced, now valid characters
std::string fixInvalidEscapes(const std::string& message);

///Run a shell command
///\warning This function executes the given string in the shell, so it _must_
///         be sanitized to avoid arbitrary code execution by users
///\param the command, including arguments, to be run
///\return all data written to standard output by the child process
std::string runCommand(const std::string& command);

///Try to get the value of an enviroment variable and store it to a string object.
///If the variable was not set \p target will not be modified. 
///\param name the name of the environment variable to get
///\param target the variable into which the environment variable should be 
///              copied, if set
///\return whether the environment variable was set
bool fetchFromEnvironment(const std::string& name, std::string& target);

///Attempt to retrieve an item from an associative container, using a default 
///value if it is not found
///\param container the container in which to search
///\param key the key for which to search
///\param def the default value to use if the key is not found
///\return the value mapped to by the key or the default value
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

///Attempt to retrieve an item from an associative container, throwing an 
///exception if it is not found
///\param container the container in which to search
///\param key the key for which to search
///\param err the message to use for the exception if the key is not found
///\return the value mapped to by the key
///\throws std::runtime_error
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
  
///Split a string into separate strings delimited by newlines
std::vector<std::string> string_split_lines(const std::string& text);

///Split a string at delimiter characters
///\param line the original string
///\param delim the character to use for splitting
///\param keepEmpty whether to output empty tokens when two delimiter characters 
///       are encountered in a row
///\param the sections of the string delimited by the given character, with all 
///       instances of that character removed
std::vector<std::string> string_split_columns(const std::string& line, char delim, 
                                              bool keepEmpty=true);

///Remove leading an trailing whitespace from a string
std::string trim(const std::string& s);

///Construct a compacted YAML string with whitespace only lines and comments
///removed
std::string reduceYAML(const std::string& input);

template<typename JSONDocument>
std::string to_string(const JSONDocument& json){
	rapidjson::StringBuffer buf;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
	json.Accept(writer);
	return buf.GetString();
}


#endif //SLATE_UTILITIES_H
