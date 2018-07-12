#ifndef SLATE_HTTPREQUESTS_H
#define SLATE_HTTPREQUESTS_H

///Trivial HTTP(S) request wrappers around libcurl. 
namespace httpRequests{

///The result of an HTTP(S) request
struct Response{
	///The HTTP status code which was returned
	unsigned int status;
	///The data received as the body of the response
	std::string body;
};
	
///Make an HTTP(S) GET request
///\param url the URL to request
Response httpGet(const std::string& url);
	
///Make an HTTP(S) DELETE request
///\param url the URL to request
Response httpDelete(const std::string& url);
	
///Make an HTTP(S) PUT request
///\param url the URL to request
///\param body the data to send as the body of the request
///\param contentType the value to use for the HTTP ContentType header
Response httpPut(const std::string& url, const std::string& body, 
                 const std::string& contentType="application/octet-stream");
	
///Make an HTTP(S) POST request
///\param url the URL to request
///\param body the data to send as the body of the request
///\param contentType the value to use for the HTTP ContentType header
Response httpPost(const std::string& url, const std::string& body, 
                  const std::string& contentType="application/octet-stream");
	
}

#endif //SLATE_HTTPREQUESTS_H