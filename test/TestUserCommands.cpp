#include "test.h"

#include <fstream>

#include <crow/json.h>

TEST(UnauthenticatedListUsers){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing users with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/users");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list users without authentication should be rejected");
	
	//try listing users with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/users?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list users with invalid authentication should be rejected");	
}

TEST(DuplicateGlobusIDs){
	using namespace httpRequests;
	TestContext tc;
	
	//TODO: abstract this
	std::string adminKey;
	{
		std::string line;
		std::ifstream in("slate_portal_user");
		while(std::getline(in,line)){
			if(!line.empty())
				adminKey=line;
		}
	}
	auto createUserUrl=tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey;
	
	crow::json::wvalue request1;
	request1["apiVersion"]="v1alpha1";
	crow::json::wvalue metadata1;
	metadata1["name"]="Person1";
	metadata1["email"]="email1";
	metadata1["admin"]=false;
	metadata1["globusID"]="SomeGlobusID";
	request1["metadata"]=std::move(metadata1);
	
	auto createResp1=httpPost(createUserUrl,dump(request1));
	ENSURE_EQUAL(createResp1.status,200,
				 "First user creation request should succeed");
	
	crow::json::wvalue request2;
	request2["apiVersion"]="v1alpha1";
	crow::json::wvalue metadata2;
	metadata2["name"]="Person2";
	metadata2["email"]="email2";
	metadata2["admin"]=false;
	metadata2["globusID"]="SomeGlobusID";
	request2["metadata"]=std::move(metadata2);
	
	auto createResp2=httpPost(createUserUrl,dump(request2));
	std::cout << createResp2.body << std::endl;
	ENSURE(createResp2.status!=200,
				 "Second user creation request should be blocked");
}