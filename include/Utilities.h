#ifndef SLATE_UTILITIES_H
#define SLATE_UTILITIES_H

#include "crow.h"

#include "Entities.h"
#include "PersistentStore.h"

///\param store the database in which to look up the user
///\param token the proffered authentication token. May be NULL if missing.
const User authenticateUser(PersistentStore& store, const char* token);
VO validateVO(char* name);
Cluster validateCluster(char* name);
crow::json::wvalue generateError(const std::string& message);

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

#endif //SLATE_UTILITIES_H
