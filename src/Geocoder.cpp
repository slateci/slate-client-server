#include <Geocoder.h>

#include "rapidjson/document.h"

#include <HTTPRequests.h>

Geocoder::Geocoder(std::string serviceURL, std::string serviceAuthToken):
serviceURL(std::move(serviceURL)),serviceAuthToken(std::move(serviceAuthToken)){}

Geocoder::Geocoder(Geocoder&& other):
serviceURL(std::move(other.serviceURL)),serviceAuthToken(std::move(other.serviceAuthToken)){}

Geocoder& Geocoder::operator=(Geocoder&& other){
	if(&other!=this){
		serviceURL=std::move(other.serviceURL);
		serviceAuthToken=std::move(other.serviceAuthToken);
	}
	return *this;
}

Geocoder::GeocodeResult Geocoder::reverseLookup(const GeoLocation& loc) const{
	std::string requestURL=serviceURL+"/"+std::to_string(loc.lat)+","
		+std::to_string(loc.lon)+"?json=1&auth="+serviceAuthToken;
	auto response=httpRequests::httpGet(requestURL);
	GeocodeResult result;
	rapidjson::Document resultJSON;
	
	if(response.status!=200){
		result.error="HTTP Error "+std::to_string(response.status);
		try{
			resultJSON.Parse(response.body.c_str());
			if (resultJSON.IsObject() && resultJSON.HasMember("error")
			    && resultJSON["error"].IsObject() && resultJSON["error"].HasMember("description")
			    && resultJSON["error"]["description"].IsString()) {
				result.error += std::string(": ") + resultJSON["error"]["description"].GetString();
			}
		}catch(std::runtime_error&){ /*Ignore*/ }
		return result;
	}
	
	try{
		resultJSON.Parse(response.body.c_str());
	}catch(std::runtime_error&){
		result.error="Failed to parse response JSON";
		return result;
	}
	//{ "distance" : "0.006",   "elevation" : {},   "latt" : "-89.99755",   
	//  "city" : "Amundsen-Scott South Pole Station",   "prov" : "AQ",   
	//  "geocode" : "NZSP-KXIFM",   "stnumber" : {},   "staddress" : {},   
	//  "geonumber" : "5498967182630",   "inlatt" : "-89.99760",   
	//  "timezone" : "Antarctica/McMurdo",   "region" : {},   "postal" : {},   
	//  "longt" : "139.27289",   "remaining_credits" : "-2",   
	//  "inlongt" : "139.27300",   "altgeocode" : "NZSP-KXIFM"}
	if(!resultJSON.IsObject()){
		result.error="Failed to parse response JSON";
		return result;
	}

	if (resultJSON.IsObject() && resultJSON.HasMember("error")
	    && resultJSON["error"].IsObject() && resultJSON["error"].HasMember("description")
	    && resultJSON["error"]["description"].IsString()) { {
		result.error = resultJSON["error"]["description"].GetString();
	}
}

	if (resultJSON.HasMember("latt") && resultJSON["latt"].IsDouble()) {
		result.lattitude = resultJSON["latt"].GetDouble();
	}
	if (resultJSON.HasMember("longt") && resultJSON["longt"].IsDouble()) {
		result.longitude = resultJSON["longt"].GetDouble();
	}

	if (resultJSON.HasMember("geocode") && resultJSON["geocode"].IsString()) {
		result.geocode = resultJSON["geocode"].GetString();
	}
	if (resultJSON.HasMember("geonumber") && resultJSON["geonumber"].IsUint64()) {
		result.geonumber = resultJSON["geonumber"].GetUint64();
	}

	if (resultJSON.HasMember("timezone") && resultJSON["timezone"].IsString()) {
		result.timezone = resultJSON["timezone"].GetString();
	}
	if (resultJSON.HasMember("city") && resultJSON["city"].IsString()) {
		result.city = resultJSON["city"].GetString();
	}
	if (resultJSON.HasMember("prov") && resultJSON["prov"].IsString()) {
		result.countryCode = resultJSON["prov"].GetString();
	}
	if (resultJSON.HasMember("countryname") && resultJSON["countryname"].IsString()) {
		result.countryName = resultJSON["countryname"].GetString();
	} else if (resultJSON.HasMember("country") && resultJSON["country"].IsString()) {
		result.countryName = resultJSON["country"].GetString();
	}
		
	return result;
}