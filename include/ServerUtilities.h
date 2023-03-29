#ifndef SLATE_SERVER_UTILITIES_H
#define SLATE_SERVER_UTILITIES_H

#include "crow.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <sstream>
#include "Entities.h"
#include "Utilities.h"

///\return a timestamp rendered as a string with format "YYYY-mmm-DD HH:MM:SS UTC"
std::string timestamp();

///Construct a JSON error object
///\param message the explanation to include in the error
///\return a JSON object with a 'kind' of "Error"
std::string generateError(const std::string& message);

///Replace escaped characters with appropriate character to create valid yaml
///\param message the string to replace escaped characters in
///\return a string with replaced, now valid characters
std::string unescape(const std::string& message);

///'Escape' single quotes in a string so that it can safely be single quoted.
std::string shellEscapeSingleQuotes(const std::string& raw);

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
					      bool keepEmpty = true);

///Remove leading an trailing whitespace from a string
///\param s string to trim
///\returns string s with any trailing whitespace removed
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

///Parse a string like 256Mi or 256Ki into an integer representation
///\param input string with integers followed by a suffix like (Ki, Gi, Mi, etc)
///\returns an long representation of output
long parseStringSuffix(const std::string& input);


///Generate a string with suffixed values (e.g. given 5120 return 5Ki)
///\param input long with value to use for string generation
///\returns string with integers followed by a suffix like (Ki, Gi, Mi, etc)
std::string generateSuffixedString(long value);

#endif //SLATE_SERVER_UTILITIES_H
