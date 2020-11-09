#ifndef SLATE_ENTITIES_H
#define SLATE_ENTITIES_H

#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <map>

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
	std::string phone;
	std::string institution;
	std::string token;
	std::string globusID;
	bool admin;
	
	explicit operator bool() const{ return valid; }
};

bool operator==(const User& u1, const User& u2);
bool operator!=(const User& u1, const User& u2);
std::ostream& operator<<(std::ostream& os, const User& u);

struct Group{
	Group():valid(false){}
	explicit Group(std::string name):valid(true),name(std::move(name)){}
	
	///Indicates whether the Group exists/is valid
	bool valid;
	std::string id;
	std::string name;
	std::string email;
	std::string phone;
	std::string scienceField;
	std::string description;
	
	explicit operator bool() const{ return valid; }
	///Get the prefix used for namesapces
	static std::string namespacePrefix(){ return "slate-group-"; }
	///Get the namespace name corresponding to this group
	std::string namespaceName() const{ return namespacePrefix()+name; }
};

///Compare groups by ID
bool operator==(const Group& v1, const Group& v2);
std::ostream& operator<<(std::ostream& os, const Group& group);

namespace std{
template<>
struct hash<Group>{
	using result_type=std::size_t;
	using argument_type=Group;
	result_type operator()(const argument_type& a) const{
		return(std::hash<std::string>{}(a.id));
	}
};
}

struct S3Credential{
	S3Credential():inUse(false),revoked(false){}
	S3Credential(std::string ak, std::string sk):
	accessKey(std::move(ak)),secretKey(std::move(sk)),inUse(false),revoked(false){}

	std::string accessKey;
	std::string secretKey;
	bool inUse;
	bool revoked;
	
	explicit operator bool() const{ return !accessKey.empty() && !secretKey.empty(); }
	std::string serialize() const;
	static S3Credential deserialize(const std::string& data);
};

bool operator==(const S3Credential& c1, const S3Credential& c2);
std::ostream& operator<<(std::ostream& os, const S3Credential& cred);

struct Cluster{
	Cluster():valid(false){}
	explicit Cluster(std::string name):valid(true),name(std::move(name)){}
	
	///Indicates whether the cluster exists/is valid
	bool valid;
	std::string id;
	std::string name;
	std::string config;
	std::string systemNamespace;
	std::string owningGroup;
	std::string owningOrganization;
	S3Credential monitoringCredential;
	
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

///A physical location on the Earth
struct GeoLocation{
	double lat, lon;
	std::string description;
};

namespace std{
template<>
struct hash<GeoLocation>{
	using result_type=std::size_t;
	using argument_type=GeoLocation;
	result_type operator()(const argument_type& a) const{
		return(std::hash<double>{}(a.lat)^std::hash<double>{}(a.lon));
	}
};
}

std::ostream& operator<<(std::ostream& os, const GeoLocation& gl);
std::istream& operator>>(std::istream& is, GeoLocation& gl);

///Represents a deployable application
struct Application{
	Application():valid(false){}
	explicit Application(std::string name):valid(true),name(std::move(name)){}
	Application(std::string name, std::string version, std::string chartVersion):
	valid(true),name(std::move(name)),version(std::move(version)),chartVersion(std::move(chartVersion)){}
	Application(std::string name, std::string version, std::string chartVersion, std::string description):
	valid(true),name(std::move(name)),version(std::move(version)),chartVersion(std::move(chartVersion)),description(std::move(description)){}
	
	///Indicates whether the application exists/is valid
	bool valid;
	std::string name;
	std::string version;
	std::string chartVersion;
	std::string description;
	
	explicit operator bool() const{ return valid; }
	
	enum Repository{
		MainRepository,
		DevelopmentRepository,
		TestRepository,
	};
};

namespace std{
template<>
struct hash<Application>{
	using result_type=std::size_t;
	using argument_type=Application;
	result_type operator()(const argument_type& a) const{
		return(std::hash<std::string>{}(a.name)^std::hash<std::string>{}(a.chartVersion));
	}
};
}

bool operator==(const Application& a1, const Application& a2);
std::ostream& operator<<(std::ostream& os, const Application& a);

///Represents a deployed/running application instance
struct ApplicationInstance{
	ApplicationInstance():valid(false){}
	
	///Indicates whether the instance exists/is valid
	bool valid;
	std::string id;
	std::string name;
	std::string application;
	std::string owningGroup;
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
			delete[] data;
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
	std::string group;
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

///Represents a PersistentVolume in Kubernetes
struct PersistentVolumeClaim{
	PersistentVolumeClaim():valid(false){}
	
	enum AccessMode{
		ReadWriteOnce,
		ReadOnlyMany,
		ReadWriteMany
	};
	
	enum VolumeMode{
		Filesystem,
		Block
	};

	bool valid;
	std::string id;
	std::string name;
	std::string group;
	std::string cluster;
	std::string storageRequest;
	AccessMode accessMode;
	VolumeMode volumeMode;
	std::string storageClass;
	std::string selectorMatchLabel;
	std::string ctime;
	std::vector<std::string> selectorLabelExpressions;
	

	/*
	* Supports multiple labels seperated by a comma
	*   "key1 : value1, key2 : value2"
	*/
	std::vector<std::string> getMatchLabelsAsVector() {
		std::string str = selectorMatchLabel;
		std::vector<std::string> ml;

		//split string first by commas and place in temp
		std::vector<std::string> temp;
		size_t pos = 0;
		std::string substring;
		//remove all whitespace
		str.erase(std::remove_if(str.begin(), str.end(), isspace), str.end());
		while ((pos = str.find(",")) != std::string::npos) {
			substring = str.substr(0, pos);
			temp.push_back(substring);
			// pos + 1 to erase comma as well
			str.erase(0, pos + 1);
		}
		//grab last or only entry (no commas)
		temp.push_back(str);	

		//split temp strings by ':' and place in ml vector
		for(std::string s : temp) {
			pos = s.find(":");
			ml.push_back(s.substr(0, pos));
			ml.push_back(s.substr(pos+1));
		}

		return ml;
	}

	/*
	*	input value of "key: value, operator: In, value2, value3, value4"
	*/
	std::vector<std::string> getSelectorLabelExpressions() {
		std::vector<std::string> le;

		// use default values if empty 
		// https://kubernetes.io/docs/concepts/overview/working-with-objects/labels/#resources-that-support-set-based-requirements
		if (selectorLabelExpressions.empty()) {
			le.push_back("key");
			le.push_back("key");
			le.push_back("operator");
			le.push_back("In");
			le.push_back("value");
			return le;
		}


		// get first key:value pair
		std::string str1 = selectorLabelExpressions[0];
		// remove white space
		str1.erase(std::remove_if(str1.begin(), str1.end(), isspace), str1.end());	
		size_t pos = 0;
		pos = str1.find(":");
		le.push_back(str1.substr(0, pos));
		le.push_back(str1.substr(pos+1));

		//if only one key/value pair, set defaults for operator and values
		if (selectorLabelExpressions.size() == 1) { 
			le.push_back("operator");
			le.push_back("In");
			le.push_back("value");
			return le;
		}

		// set operator and values
		std::string str2 = selectorLabelExpressions[1];
		str2.erase(std::remove_if(str2.begin(), str2.end(), isspace), str2.end());	
		pos = str2.find(":");
		//must be 'operator'
		le.push_back("operator");
		le.push_back(str2.substr(pos+1));
		for(size_t i = 2; i < selectorLabelExpressions.size(); i++) {
			le.push_back(selectorLabelExpressions[i]);
		}

		return le;
	}

	explicit operator bool() const{ return valid; }
};

///Compare volumes by ID
bool operator==(const PersistentVolumeClaim& v1, const PersistentVolumeClaim& v2);
std::ostream& operator<<(std::ostream& os, const PersistentVolumeClaim& v);

std::string to_string(PersistentVolumeClaim::AccessMode mode);
PersistentVolumeClaim::AccessMode accessModeFromString(const std::string& s);
std::string to_string(PersistentVolumeClaim::VolumeMode mode);
PersistentVolumeClaim::VolumeMode volumeModeFromString(const std::string& s);

namespace std{
	template<>
	struct hash<PersistentVolumeClaim>{
		using result_type=std::size_t;
		using argument_type=PersistentVolumeClaim;
		result_type operator()(const argument_type& s) const{
			return(std::hash<std::string>{}(s.id));
		}
	};
}

static class IDGenerator{
public:
	///Creates a random ID for a new user
	std::string generateUserID(){
		return userIDPrefix+generateRawID();
	}
	///Creates a random ID for a new cluster
	std::string generateClusterID(){
		return clusterIDPrefix+generateRawID();
	}
	///Creates a random ID for a new group
	std::string generateGroupID(){
		return groupIDPrefix+generateRawID();
	}
	///Creates a random ID for a new application instance
	std::string generateInstanceID(){
		return instanceIDPrefix+generateRawID();
	}
	///Creates a random ID for a new secret
	std::string generateSecretID(){
		return secretIDPrefix+generateRawID();
	}
	///Creates a random ID for a new volume
	std::string generateVolumeID(){
		return volumeIDPrefix+generateRawID();
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
		return generateRawID()+generateRawID();
	}
	
	const static std::string userIDPrefix;
	const static std::string clusterIDPrefix;
	const static std::string groupIDPrefix;
	const static std::string instanceIDPrefix;
	const static std::string secretIDPrefix;
	const static std::string volumeIDPrefix;
	
private:
	std::mutex mut;
	std::random_device idSource;
	
	std::string generateRawID();
} idGenerator;

#endif //SLATE_ENTITIES_H
