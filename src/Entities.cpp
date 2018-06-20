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
