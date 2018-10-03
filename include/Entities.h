#ifndef SLATE_ENTITIES_H
#define SLATE_ENTITIES_H

#include <mutex>
#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

extern "C"{
	#include <scrypt/util/insecure_memzero.h>
}

///Represents a user account
struct User{
	User():valid(false),admin(false){}
	explicit User(std::string name):
	valid(true),name(std::move(name)){}
	
	///Indicates whether the account exists/is valid
	bool valid;
	std::string id;
	std::string name;
	std::string email;
	std::string token;
	std::string globusID;
	bool admin;
	
	explicit operator bool() const{ return valid; }
};

bool operator==(const User& u1, const User& u2);
bool operator!=(const User& u1, const User& u2);
std::ostream& operator<<(std::ostream& os, const User& u);

struct VO{
	VO():valid(false){}
	explicit VO(std::string name):valid(true),name(std::move(name)){}
	
	///Indicates whether the VO exists/is valid
	bool valid;
	std::string id;
	std::string name;
	
	explicit operator bool() const{ return valid; }
	///Get the prefix used for namesapces
	static std::string namespacePrefix(){ return "slate-vo-"; }
	///Get the namespace name corresponding to this VO
	std::string namespaceName() const{ return namespacePrefix()+name; }
};

///Compare VOs by ID
bool operator==(const VO& v1, const VO& v2);
std::ostream& operator<<(std::ostream& os, const VO& vo);

namespace std{
template<>
struct hash<VO>{
	using result_type=std::size_t;
	using argument_type=VO;
	result_type operator()(const argument_type& a) const{
		return(std::hash<std::string>{}(a.id));
	}
};
}

struct Cluster{
	Cluster():valid(false){}
	explicit Cluster(std::string name):valid(true),name(std::move(name)){}
	
	///Indicates whether the cluster exists/is valid
	bool valid;
	std::string id;
	std::string name;
	std::string config;
	std::string systemNamespace;
	std::string owningVO;
	
	explicit operator bool() const{ return valid; }
};

///Compare Clusters by ID
bool operator==(const Cluster& c1, const Cluster& c2);
std::ostream& operator<<(std::ostream& os, const Cluster& c);

namespace std{
template<>
struct hash<Cluster>{
	using result_type=std::size_t;
	using argument_type=Cluster;
	result_type operator()(const argument_type& a) const{
		return(std::hash<std::string>{}(a.id));
	}
};
}

///Represents a deployable application
struct Application{
	Application():valid(false){}
	explicit Application(std::string name):valid(true),name(std::move(name)){}
	
	///Indicates whether the application exists/is valid
	bool valid;
	std::string name;
	
	explicit operator bool() const{ return valid; }
	
	enum Repository{
		MainRepository,
		DevelopmentRepository,
		TestRepository,
	};
};

std::ostream& operator<<(std::ostream& os, const Application& a);

///Represents a deployed/running application instance
struct ApplicationInstance{
	ApplicationInstance():valid(false){}
	
	///Indicates whether the instance exists/is valid
	bool valid;
	std::string id;
	std::string name;
	std::string application;
	std::string owningVO;
	std::string cluster;
	std::string config;
	std::string ctime;
	
	explicit operator bool() const{ return valid; }
};

///Compare ApplicationInsatnces by ID
bool operator==(const ApplicationInstance& i1, const ApplicationInstance& i2);
std::ostream& operator<<(std::ostream& os, const ApplicationInstance& a);

namespace std{
template<>
struct hash<ApplicationInstance>{
	using result_type=std::size_t;
	using argument_type=ApplicationInstance;
	result_type operator()(const argument_type& a) const{
		return(std::hash<std::string>{}(a.id));
	}
};
}

///Unencrypted secret data
///Zeros out the data buffer before freeing it to try to keep it from being left
///around in memory.
struct SecretData{
	explicit SecretData(std::size_t s):data(new char[s],deleter{s}),dataSize(s){}
	
	struct deleter{
		std::size_t dataSize;
		void operator()(char* data){
			//TODO: can we assume memset_s is available and use that?
			insecure_memzero(data,dataSize);
			delete data;
		}
	};
	
	std::unique_ptr<char[],deleter> data;
	std::size_t dataSize;
};

///Represents sensitive user data to be stored in kubernetes
struct Secret{
	Secret():valid(false){}
	
	bool valid;
	std::string id;
	std::string name;
	std::string vo;
	std::string cluster;
	std::string ctime;
	///The encrypted secret data
	std::string data;
	
	explicit operator bool() const{ return valid; }
};

///Compare Secrets by ID
bool operator==(const Secret& s1, const Secret& s2);
std::ostream& operator<<(std::ostream& os, const Secret& s);

namespace std{
template<>
struct hash<Secret>{
	using result_type=std::size_t;
	using argument_type=Secret;
	result_type operator()(const argument_type& s) const{
		return(std::hash<std::string>{}(s.id));
	}
};
}

static class IDGenerator{
public:
	///Creates a random ID for a new user
	std::string generateUserID(){
		std::lock_guard<std::mutex> lock(mut);
		boost::uuids::uuid id = gen();
		return userIDPrefix+to_string(id);
	}
	///Creates a random ID for a new cluster
	std::string generateClusterID(){
		std::lock_guard<std::mutex> lock(mut);
		boost::uuids::uuid id = gen();
		return clusterIDPrefix+to_string(id);
	}
	///Creates a random ID for a new VO
	std::string generateVOID(){
		std::lock_guard<std::mutex> lock(mut);
		boost::uuids::uuid id = gen();
		return voIDPrefix+to_string(id);
	}
	///Creates a random ID for a new application instance
	std::string generateInstanceID(){
		std::lock_guard<std::mutex> lock(mut);
		boost::uuids::uuid id = gen();
		return instanceIDPrefix+to_string(id);
	}
	///Creates a random ID for a new secret
	std::string generateSecretID(){
		std::lock_guard<std::mutex> lock(mut);
		boost::uuids::uuid id = gen();
		return secretIDPrefix+to_string(id);
	}
	///Creates a random access token for a user
	///At the moment there is no apparent reason that a user's access token
	///should have any particular structure or meaning. Definite requirements:
	/// - Each user's token should be unique
	/// - There should be no way for anyone to derive or guess a user's token
	///These requirements seem adequately satisfied by a block of 
	///cryptographically random data. Note that boost::uuids::random_generator 
	///uses /dev/urandom as a source of randomness, so this is not optimally
	///secure on Linux hosts. 
	std::string generateUserToken(){
		std::lock_guard<std::mutex> lock(mut);
		boost::uuids::uuid id = gen();
		return to_string(id);
	}
	
	const static std::string userIDPrefix;
	const static std::string clusterIDPrefix;
	const static std::string voIDPrefix;
	const static std::string instanceIDPrefix;
	const static std::string secretIDPrefix;
	
private:
	std::mutex mut;
	boost::uuids::random_generator gen;
} idGenerator;

#endif //SLATE_ENTITIES_H
