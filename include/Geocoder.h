#ifndef SLATE_GEOCODER_H
#define SLATE_GEOCODER_H

#include <string>
#include "Entities.h"

class Geocoder{
public:
	explicit Geocoder(std::string serviceURL="", std::string serviceAuthToken="");
	Geocoder(Geocoder&& other);
	
	Geocoder& operator=(Geocoder&& other);
	
	bool canGeocode() const{ return !serviceURL.empty() && !serviceAuthToken.empty(); }
	
	struct GeocodeResult{
		double latitude;
		double longitude;
		
		std::string error;
		
		std::string geocode;
		uint64_t geonumber;
		
		std::string timezone;
		std::string city;
		std::string countryCode;
		std::string countryName;
		
		explicit operator bool() const{ return error.empty(); }
	};
	
	GeocodeResult reverseLookup(const GeoLocation& loc) const;
private:
	std::string serviceURL;
	std::string serviceAuthToken;
};

#endif //SLATE_GEOCODER_H