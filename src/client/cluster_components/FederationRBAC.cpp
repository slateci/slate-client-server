#include <client/Client.h>

#include <iostream>

#include <Process.h>
#include <Utilities.h>

const std::string Client::federationRoleURL="https://jenkins.slateci.io/artifacts/test/federation-role.yaml";

Client::ClusterComponent::ComponentStatus Client::checkFederationRBAC(const std::string& configPath, const std::string& systemNamespace) const{
	static const std::string rbacVersionTag="slate-federation-role-version";

	//find out what the RBAC is supposed to be
	auto download=httpRequests::httpGet(federationRoleURL,defaultOptions());
	if(download.status!=200)
		throw std::runtime_error("Failed to download current RBAC manifest from "+federationRoleURL);
	
	//At this point we really want to parse the YAML that we downloaded. However,
	//a YAML parser would be a substantial new dependency for the client, so for
	//now we 'parse' the relevant data out manually.
	std::string rbacVersion="unknown";
	{
		//my kingdom for a working regex library
		auto label=rbacVersionTag+": \"";
		auto pos=download.body.find(label);
		if(pos==std::string::npos)
			throw std::runtime_error("Version information not found in current RBAC manifest from "+federationRoleURL);
		pos+=label.size();
		if(pos>=download.body.size())
			throw std::runtime_error("Version information not found in current RBAC manifest from "+federationRoleURL);
		auto end=download.body.find('\"',pos);
		rbacVersion=download.body.substr(pos,end!=std::string::npos?end-pos:end);
	}

	auto result=runCommand("kubectl",{"get","clusterroles",
	                                  "-l=slate-federation-role-version",
	                                  "-o=json",
	                                  "--kubeconfig",configPath});
	if(result.status!=0)
		throw std::runtime_error("kubectl failed: "+result.error);
	
	rapidjson::Document json;
	json.Parse(result.output.c_str());
	if(!json.HasMember("items") || !json["items"].IsArray())
		throw std::runtime_error("Malformed JSON from kubectl");
	
	if(json["items"].Empty()){ //found nothing
		//try looking for a version too old to have the version label
		result=runCommand("kubectl",{"get","clusterrole",
		                              "federation-cluster",
		                              "-o=json",
		                              "--kubeconfig",configPath});
		if(result.status!=0){
			if(result.error.find("Error from server (NotFound)")!=std::string::npos)
				return ClusterComponent::NotInstalled;
			throw std::runtime_error("kubectl failed: "+result.error);
		}
		json.Parse(result.output.c_str());
	
		return (ClusterComponent::OutOfDate);
	}
	
	if(json["items"].Size()!=2) //if exactly two clusterroles are not found, something is not right
		return ClusterComponent::NotInstalled;
	for(const auto& item : json["items"].GetArray()){
		if(!item.IsObject() || !item.HasMember("metadata") || !item["metadata"].IsObject()
		   || !item["metadata"].HasMember("labels") || !item["metadata"]["labels"].IsObject()
		   || !item["metadata"]["labels"].HasMember("slate-federation-role-version")
		   || !item["metadata"]["labels"]["slate-federation-role-version"].IsString())
			continue; //this should be unreachable
		std::string installedVersion=item["metadata"]["labels"]["slate-federation-role-version"].GetString();
		int verComp=compareVersions(installedVersion,rbacVersion);
		switch(verComp){
			case -1:
				return ClusterComponent::OutOfDate;
			case 0:
				return ClusterComponent::UpToDate;
			case 1:
				throw std::runtime_error("Encountered component version from the future! "
				                         "Is this client out of date (try `slate version upgrade`)?");
			default:
				throw std::runtime_error("Internal error: invalid version comparison result");
		}
	}
	return ClusterComponent::OutOfDate;
}

void Client::installFederationRBAC(const std::string& configPath, const std::string& systemNamespace) const{
	std::cout << "Applying " << federationRoleURL << std::endl;
	auto result=runCommand("kubectl",{"apply","-f",federationRoleURL,"--kubeconfig",configPath});
	if(result.status)
		throw std::runtime_error("Failed to deploy federation clusterrole: "+result.error);
}

void Client::removeFederationRBAC(const std::string& configPath, const std::string& systemNamespace) const{
	auto result=runCommand("kubectl",{"delete","-f",federationRoleURL,"--kubeconfig",configPath});
	if(result.status)
		throw std::runtime_error("Failed to delete federation clusterrole: "+result.error);
}
