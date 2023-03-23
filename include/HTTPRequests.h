#ifndef SLATE_HTTPREQUESTS_H
#define SLATE_HTTPREQUESTS_H

#include <map>
#include <string>

#include <curl/curlver.h>

#ifdef CURL_AT_LEAST_VERSION
#if CURL_AT_LEAST_VERSION(7, 62, 0)
#define SLATE_EXTRACT_HOSTNAME_AVAIL 1
#endif
#endif

///Trivial HTTP(S) request wrappers around libcurl. 
namespace httpRequests{

struct Options{
	Options():contentType("application/octet-stream"){}
	///value to use for the HTTP ContentType header.
	///Only meaningful for POST and PUT operations
	std::string contentType;
	///If non-empty, the value to set as curl's CURLOPT_CAINFO for SSL 
	///certificate verification. 
	std::string caBundlePath;
};
	
///The result of an HTTP(S) request
struct Response{
	///The HTTP status code which was returned
	unsigned int status;
	///The data received as the body of the response
	std::string body;
};
	
///Make an HTTP(S) GET request
///\param url the URL to request
Response httpGet(const std::string& url, const Options& options={});
	
///Make an HTTP(S) DELETE request
///\param url the URL to request
Response httpDelete(const std::string& url, const Options& options={});
	
///Make an HTTP(S) PUT request
///\param url the URL to request
///\param body the data to send as the body of the request
///\param options ContentType and CA settings
Response httpPut(const std::string& url, const std::string& body, 
                 const Options& options={});
	
///Make an HTTP(S) POST request
///\param url the URL to request
///\param body the data to send as the body of the request
	///\param options ContentType and CA settings
Response httpPost(const std::string& url, const std::string& body, 
                  const Options& options={});

///Make an HTTP(S) POST request with form data
///\param url the URL to request
///\param body the data to send as the body of the request
///\param formData the form fields and their values to send
	///\param options CA settings; ContentType 
Response httpPostForm(const std::string& url, 
                      const std::multimap<std::string,std::string>& formData, 
                      const Options& options={});

#ifdef SLATE_EXTRACT_HOSTNAME_AVAIL
///Get the hostname component from a URL. 
///\throws std::invalid_argument if \p url cannot be parsed as a URL.
std::string extractHostname(const std::string& url);
#endif
	
}

#endif //SLATE_HTTPREQUESTS_H
