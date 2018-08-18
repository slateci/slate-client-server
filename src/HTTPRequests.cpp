#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>

#include <curl/curl.h>

#include "HTTPRequests.h"

namespace httpRequests{

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
size_t collectCurlOutput(void* buffer, size_t size, size_t nmemb, void* userp){
	CurlOutputData& data=*static_cast<CurlOutputData*>(userp);
	//curl can't tolerate exceptions, so stop them and log them to stderr here
	try{
		data.output.append((char*)buffer,size*nmemb);
	}catch(std::exception& ex){
		std::cerr << data.context << " Exception thrown while collecting output: " 
		  << ex.what() << std::endl;
		return(size*nmemb?0:1); //return a different number to indicate error
	}catch(...){
		std::cerr << data.context << " Exception thrown while collecting output" << std::endl;
		return(size*nmemb?0:1); //return a different number to indicate error
	}
	return(size*nmemb);//return full size to indicate success
}

///Callback function for sending data to libcurl, and only to be called by libcurl. 
///See https://curl.haxx.se/libcurl/c/CURLOPT_READFUNCTION.html
///\param buffer the location to which data is to be written
///\param size the maximum number of 'items' which can be written
///\param nmemb the size of each 'item' which can be written
///\param userp pointer to a CurlInputData object with the data to be sent and error context
///             information
auto sendCurlInput(char* buffer, size_t size, size_t nitems, void* userp)->size_t{
	CurlInputData& data=*static_cast<CurlInputData*>(userp);
	if(data.input.eof())
		return(0);
	data.input.read((char*)buffer, size*nitems);
	if(data.input.fail() && !data.input.eof()){
		std::cerr << data.context << " Error reading input data" << std::endl;
		return(CURL_READFUNC_ABORT);
	}
	return(data.input.gcount());
}

///Attempt to extract an error code from libcurl into a more meaningful message, and throw it as an
///exception. Never returns normally. 
///\param expl any contextual information to be prepended to the error message
///\param err the error code returned by libcurl
///\param errBuf a buffer to which libcurl may have written an explanatory message
[[ noreturn ]] void reportCurlError(std::string expl, CURLcode err, const char* errBuf){
	if(errBuf[0]!=0)
		throw std::runtime_error(expl+"\n curl error: "+errBuf);
	else
		throw std::runtime_error(expl+"\n curl error: "+curl_easy_strerror(err));
}

} //namespace detail

Response httpGet(const std::string& url, const Options& options){
	detail::CurlOutputData data{{},"GET "+url};
	
	CURLcode err;
	std::unique_ptr<char[]> errBuf(new char[CURL_ERROR_SIZE]);
	errBuf[0]=0;
	std::unique_ptr<CURL,void (*)(CURL*)> curlSession(curl_easy_init(),curl_easy_cleanup);
	using detail::reportCurlError;
	
	err=curl_easy_setopt(curlSession.get(), CURLOPT_ERRORBUFFER, errBuf.get());
	if(err!=CURLE_OK)
		throw std::runtime_error("Failed to set curl error buffer");
	err=curl_easy_setopt(curlSession.get(), CURLOPT_URL, url.c_str());
	if(err!=CURLE_OK)
		detail::reportCurlError("Failed to set curl URL option",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_HTTPGET, 1);
	if(err!=CURLE_OK)
		detail::reportCurlError("Failed to set curl GET option",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_WRITEFUNCTION, detail::collectCurlOutput);
	if(err!=CURLE_OK)
		detail::reportCurlError("Failed to set curl output callback",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_WRITEDATA, &data);
	if(err!=CURLE_OK)
		detail::reportCurlError("Failed to set curl output callback data",err,errBuf.get());
	if(!options.caBundlePath.empty()){
		err=curl_easy_setopt(curlSession.get(), CURLOPT_CAINFO, options.caBundlePath.c_str());
		if(err!=CURLE_OK)
			reportCurlError("Failed to set curl CA bundle path",err,errBuf.get());
	}
	err=curl_easy_perform(curlSession.get());
	if(err!=CURLE_OK)
		detail::reportCurlError("curl perform GET failed",err,errBuf.get());
		
	long code;
	err=curl_easy_getinfo(curlSession.get(),CURLINFO_RESPONSE_CODE,&code);
	if(err!=CURLE_OK)
		detail::reportCurlError("Failed to get HTTP response code from curl",err,errBuf.get());
	assert(code>=0);
		
	return Response{(unsigned int)code,data.output};
}

Response httpDelete(const std::string& url, const Options& options){
	detail::CurlOutputData data{{},"DELETE "+url};
	
	CURLcode err;
	std::unique_ptr<char[]> errBuf(new char[CURL_ERROR_SIZE]);
	errBuf[0]=0;
	std::unique_ptr<CURL,void (*)(CURL*)> curlSession(curl_easy_init(),curl_easy_cleanup);
	using detail::reportCurlError;
	
	err=curl_easy_setopt(curlSession.get(), CURLOPT_ERRORBUFFER, errBuf.get());
	if(err!=CURLE_OK)
		throw std::runtime_error("Failed to set curl error buffer");
	err=curl_easy_setopt(curlSession.get(), CURLOPT_URL, url.c_str());
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl URL option",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_CUSTOMREQUEST, "DELETE");
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl DELETE option",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_WRITEFUNCTION, detail::collectCurlOutput);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl output callback",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_WRITEDATA, &data);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl output callback data",err,errBuf.get());
	if(!options.caBundlePath.empty()){
		err=curl_easy_setopt(curlSession.get(), CURLOPT_CAINFO, options.caBundlePath.c_str());
		if(err!=CURLE_OK)
			reportCurlError("Failed to set curl CA bundle path",err,errBuf.get());
	}
	err=curl_easy_perform(curlSession.get());
	if(err!=CURLE_OK)
		reportCurlError("curl perform GET failed",err,errBuf.get());
		
	long code;
	err=curl_easy_getinfo(curlSession.get(),CURLINFO_RESPONSE_CODE,&code);
	if(err!=CURLE_OK)
		reportCurlError("Failed to get HTTP response code from curl",err,errBuf.get());
	assert(code>=0);
		
	return Response{(unsigned int)code,data.output};
}

Response httpPut(const std::string& url, const std::string& body, 
                 const Options& options){
	curl_off_t dataSize=body.size();
	detail::CurlInputData input(body,"PUT "+url);
	detail::CurlOutputData output{{},"PUT "+url};
	
	CURLcode err;
	std::unique_ptr<char[]> errBuf(new char[CURL_ERROR_SIZE]);
	errBuf[0]=0;
	std::unique_ptr<CURL,void (*)(CURL*)> curlSession(curl_easy_init(),curl_easy_cleanup);
	using detail::reportCurlError;
	
	err=curl_easy_setopt(curlSession.get(), CURLOPT_ERRORBUFFER, errBuf.get());
	if(err!=CURLE_OK)
		throw std::runtime_error("Failed to set curl error buffer");
	err=curl_easy_setopt(curlSession.get(), CURLOPT_URL, url.c_str());
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl URL option",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_UPLOAD, 1);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl PUT/upload option",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_READFUNCTION, detail::sendCurlInput);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl input callback",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_READDATA, &input);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl input callback data",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_INFILESIZE_LARGE, dataSize);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl input data size",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_WRITEFUNCTION, detail::collectCurlOutput);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl output callback",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_WRITEDATA, &output);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl output callback data",err,errBuf.get());
	std::unique_ptr<curl_slist,void (*)(curl_slist*)> headerList(nullptr,curl_slist_free_all);
	headerList.reset(curl_slist_append(headerList.release(),("Content-Type: "+options.contentType).c_str()));
	err=curl_easy_setopt(curlSession.get(), CURLOPT_HTTPHEADER, headerList.get());
	if(err!=CURLE_OK)
		reportCurlError("Failed to set request headers",err,errBuf.get());
	if(!options.caBundlePath.empty()){
		err=curl_easy_setopt(curlSession.get(), CURLOPT_CAINFO, options.caBundlePath.c_str());
		if(err!=CURLE_OK)
			reportCurlError("Failed to set curl CA bundle path",err,errBuf.get());
	}
		
	err=curl_easy_perform(curlSession.get());
	if(err!=CURLE_OK)
		reportCurlError("curl perform PUT failed",err,errBuf.get());
		
	long code;
	err=curl_easy_getinfo(curlSession.get(),CURLINFO_RESPONSE_CODE,&code);
	if(err!=CURLE_OK)
		reportCurlError("Failed to get HTTP response code from curl",err,errBuf.get());
	assert(code>=0);
		
	return Response{(unsigned int)code,output.output};
}

Response httpPost(const std::string& url, const std::string& body, 
                  const Options& options){
	curl_off_t dataSize=body.size();
	detail::CurlOutputData output{{},"POST "+url};
	
	CURLcode err;
	std::unique_ptr<char[]> errBuf(new char[CURL_ERROR_SIZE]);
	errBuf[0]=0;
	std::unique_ptr<CURL,void (*)(CURL*)> curlSession(curl_easy_init(),curl_easy_cleanup);
	using detail::reportCurlError;
	
	err=curl_easy_setopt(curlSession.get(), CURLOPT_ERRORBUFFER, errBuf.get());
	if(err!=CURLE_OK)
		throw std::runtime_error("Failed to set curl error buffer");
	err=curl_easy_setopt(curlSession.get(), CURLOPT_URL, url.c_str());
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl URL option",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_POSTFIELDS, body.c_str());
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl POST data",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_POSTFIELDSIZE_LARGE, dataSize);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl POST data size",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_WRITEFUNCTION, detail::collectCurlOutput);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl output callback",err,errBuf.get());
	err=curl_easy_setopt(curlSession.get(), CURLOPT_WRITEDATA, &output);
	if(err!=CURLE_OK)
		reportCurlError("Failed to set curl output callback data",err,errBuf.get());	
	std::unique_ptr<curl_slist,void (*)(curl_slist*)> headerList(nullptr,curl_slist_free_all);
	headerList.reset(curl_slist_append(headerList.release(),("Content-Type: "+options.contentType).c_str()));
	err=curl_easy_setopt(curlSession.get(), CURLOPT_HTTPHEADER, headerList.get());
	if(err!=CURLE_OK)
		reportCurlError("Failed to set request headers",err,errBuf.get());
	if(!options.caBundlePath.empty()){
		err=curl_easy_setopt(curlSession.get(), CURLOPT_CAINFO, options.caBundlePath.c_str());
		if(err!=CURLE_OK)
			reportCurlError("Failed to set curl CA bundle path",err,errBuf.get());
	}
		
	err=curl_easy_perform(curlSession.get());
	if(err!=CURLE_OK)
		reportCurlError("curl perform POST failed",err,errBuf.get());
		
	long code;
	err=curl_easy_getinfo(curlSession.get(),CURLINFO_RESPONSE_CODE,&code);
	if(err!=CURLE_OK)
		reportCurlError("Failed to get HTTP response code from curl",err,errBuf.get());
	assert(code>=0);
		
	return Response{(unsigned int)code,output.output};
}

} //namespace httpRequests
