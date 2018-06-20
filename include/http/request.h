#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>

#include <curl/curl.h>

#include <aws/url.h>

///Trivial HTTP(S) request wrappers around libcurl. 
namespace httpRequests{

///The result of an HTTP(S) request
struct Response{
	///The HTTP status code which was returned
	unsigned int status;
	///The data received as the body of the response
	std::string body;
};

namespace detail{

///Helper data used for collecting output data from libcurl
struct CurlOutputData{
	///The collected output, should be empty initially
	std::string output;
	///Context information to be included in messages if an error occurs
	std::string context;
};

///Helper data used for sending input data to libcurl
struct CurlInputData{
	///Stream containing data to be given to libcurl
	std::istringstream input;
	///Context information to be included in messages if an error occurs
	std::string context;
	
	///\param data the data to be given to libcurl
	///\param context context information to be included in messages if an error occurs
	CurlInputData(const std::string& data, std::string context):
	input(data),context(context){}
};

///Callback function for collecting data from libcurl, and only to be called by libcurl. 
///See https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
///\param buffer the data being provided by libcurl
///\param size the number of 'items' in the available data
///\param nmemb the size of each 'item' of available data
///\param userp pointer to a CurlOutputData object with the buffer where data is to be collected and
///             error context information
size_t collectCurlOutput(void* buffer, size_t size, size_t nmemb, void* userp);

///Callback function for sending data to libcurl, and only to be called by libcurl. 
///See https://curl.haxx.se/libcurl/c/CURLOPT_READFUNCTION.html
///\param buffer the location to which data is to be written
///\param size the maximum number of 'items' which can be written
///\param nmemb the size of each 'item' which can be written
///\param userp pointer to a CurlInputData object with the data to be sent and error context
///             information
size_t sendCurlInput(char* buffer, size_t size, size_t nitems, void* userp);

///Attempt to extract an error code from libcurl into a more meaningful message, and throw it as an
///exception. Never returns normally. 
///\param expl any contextual information to be prepended to the error message
///\param err the error code returned by libcurl
///\param errBuf a buffer to which libcurl may have written an explanatory message
[[ noreturn ]] void reportCurlError(std::string expl, CURLcode err, const char* errBuf);

} //namespace detail

///Make an HTTP(S) GET request
///\param url the URL to request
Response httpGet(const aws::URL& url);

///Make an HTTP(S) DELETE request
///\param url the URL to request
	Response httpDelete(const aws::URL& url);

///Make an HTTP(S) PUT request
///\param url the URL to request
///\param body the data to send as the body of the request
///\param contentType the value to use for the HTTP ContentType header
Response httpPut(const aws::URL& url, const std::string& body, 
                 const std::string& contentType="application/octet-stream");

///Make an HTTP(S) POST request
///\param url the URL to request
///\param body the data to send as the body of the request
///\param contentType the value to use for the HTTP ContentType header
Response httpPost(const aws::URL& url, const std::string& body, 
                  const std::string& contentType="application/octet-stream");

} //namespace httpRequests

#endif //HTTP_REQUEST_H