#include "Entities.h"

#include <boost/lexical_cast.hpp>

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>

bool operator==(const User& u1, const User& u2){
	return(u1.valid==u2.valid && u1.id==u2.id);
}
bool operator!=(const User& u1, const User& u2){
	return(!(u1==u2));
}

std::ostream& operator<<(std::ostream& os, const User& u){
	if(!u)
		return os << "invalid user";
	os << u.id;
	if(!u.name.empty())
		os << " (" << u.name << ')';
	return os;
}

bool operator==(const VO& v1, const VO& v2){
	return v1.id==v2.id;
}

std::ostream& operator<<(std::ostream& os, const VO& vo){
	if(!vo)
		return os << "invalid VO";
	os << vo.id;
	if(!vo.name.empty())
		os << " (" << vo.name << ')';
	return os;
}

bool operator==(const Cluster& c1, const Cluster& c2){
	return c1.id==c2.id;
}

std::ostream& operator<<(std::ostream& os, const Cluster& c){
	if(!c)
		return os << "invalid cluster";
	os << c.id;
	if(!c.name.empty())
		os << " (" << c.name << ')';
	return os;
}

std::ostream& operator<<(std::ostream& os, const Application& a){
	if(!a)
		return os << "invalid application";
	os << a.name;
	return os;
}

bool operator==(const ApplicationInstance& i1, const ApplicationInstance& i2){
	return i1.id==i2.id;
}

std::ostream& operator<<(std::ostream& os, const ApplicationInstance& a){
	if(!a)
		return os << "invalid application instance";
	os << a.id;
	if(!a.name.empty())
		os << " (" << a.name << ')';
	return os;
}

const std::string IDGenerator::userIDPrefix="User_";
const std::string IDGenerator::clusterIDPrefix="Cluster_";
const std::string IDGenerator::voIDPrefix="VO_";
const std::string IDGenerator::instanceIDPrefix="Instance_";

///Render a UUID using base 64 instead of base 16; shortening the representation 
///by 10 characters
std::string toBase64(std::string idString){
	try{
		auto id=boost::lexical_cast<boost::uuids::uuid>(idString);
		std::ostringstream os;
		using namespace boost::archive::iterators;
		using base64_text=base64_from_binary<transform_width<const unsigned char*,6,8>>;
		std::copy(base64_text(id.begin()),base64_text(id.end()),ostream_iterator<char>(os));
		return os.str();
	}catch(boost::bad_lexical_cast& blc){
		//TODO: do something more useful
		return idString;
	}
}
