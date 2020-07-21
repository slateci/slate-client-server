#include <client/Client.h>

#include <fstream>
#include <iostream>

#include "FileHandle.h"
#include "KubeInterface.h"
#include "Utilities.h"

Client::ClusterComponent::ComponentStatus Client::checkPrometheusMonitoring(const std::string& configPath, const std::string& systemNamespace) const{
	throw std::runtime_error("not implemented");
}

namespace{
	///Prompt the user for the name of the cluster, and ensure that the current 
	///kubeconfig is consistent with the answer. 
	///\param configPath the path to the kubeconfig to use
	///\param hideProgress a function returning an object, during whose lifetime
	///                    progress indication will be paused
	std::string getClusterName(const std::string& configPath, std::function<std::string(std::string)> makeURL, const httpRequests::Options& httpOptions, std::function<Client::HideProgress()> hideProgress){
		std::string clusterName;
		{
			auto quiet=hideProgress();
			std::cout << "Enter the name of the cluster for which to configure monitoring.\n"
			"This must match the cluster your kubeconfig is currently set for.\n"
			"Cluster name: ";
			std::cout.flush();
			std::getline(std::cin,clusterName);
		}
		//Check that the cluster exists and is the one we're able to work on
		{
			std::cout << "Checking status of cluster " << clusterName << "..." << std::endl;
			auto url = makeURL("clusters/"+clusterName);
			auto response=httpRequests::httpGet(url,httpOptions);
			if(response.status!=200){
				if(response.status==404){
					std::cout << "No cluster '" << clusterName << "' was found in the SLATE Federation" << std::endl;
					throw std::runtime_error("Target cluster not found");
				}
				else
				throw std::runtime_error("Failed to fetch info for cluster "+clusterName+": Error "+std::to_string(response.status));
			}
			//check that kubeconfig.server == expected server
			rapidjson::Document json;
			try{
				json.Parse(response.body.c_str());
			}catch(std::runtime_error& err){
				throw std::runtime_error("Failed to parse cluster info JSON from the SLATE API server");
			}
			if(!json.IsObject() || !json.HasMember("metadata") || !json["metadata"].IsObject())
			throw std::runtime_error("Failed to parse cluster info JSON from the SLATE API server");
			if(json["metadata"].HasMember("masterAddress") && json["metadata"]["masterAddress"].IsString()){
				std::string expectedKubeServer=json["metadata"]["masterAddress"].GetString();
				std::string currentKubeServer;
				try{
					auto kubeResult=kubernetes::kubectl(configPath,{"config","view","--minify","-o","jsonpath={.clusters[0].cluster.server}"});
					if(kubeResult.status!=0)
					throw std::runtime_error("kubectl config view failed: "+kubeResult.error);
					currentKubeServer=kubeResult.output;
				}catch(std::runtime_error& err){
					std::cerr << "kubectl is required to modify cluster components";
					throw;
				}
				if(currentKubeServer!=expectedKubeServer){
					auto quiet=hideProgress();
					std::cout << "The expected Kubernetes API server address for cluster " << clusterName
					<< " is \n\t" << expectedKubeServer << "\nbut the active kubeconfig refers to server\n\t"
					<< currentKubeServer 
					<< "\ninstead. This may indicate that you have a configuration for another cluster loaded.\n"
					<< "Are you sure you want to proceed with installing SLATE's Prometheus? y/[n]: ";
					std::cout.flush();
					std::string userResp;
					std::getline(std::cin,userResp);
					if(userResp!="y" && userResp!="Y")
					throw InstallAborted("Prometheus modification aborted");
				}
			}
		}
		return clusterName;
	}
	
	void ensureHelmPresent(){
		const std::string stableURL="https://kubernetes-charts.storage.googleapis.com";
		std::string repoListing;
		try{
			auto helmCheck=runCommand("helm",{"repo","list"});
			repoListing=helmCheck.output;
		}catch(std::runtime_error& err){
			std::cerr << "Unable to locate helm in the search path.\n"
			"If it is not installed, you can download it from "
			"https://github.com/helm/helm/releases" << std::endl;
			throw;
		}
		if(repoListing.find(stableURL)==std::string::npos)
		throw std::runtime_error("The helm stable repository at "+stableURL
								 +"does not appear to be configured. It can be added with:\n"
								 "helm repo add stable "+stableURL);
	}
	
	///\param clusterName the name of the cluster for which to look up the 
	///                   monitoring credential
	///\return a tuple consisting of the access key and secret key making up the 
	///        credential
	std::tuple<std::string,std::string> fetchMonitoringCredential(const std::string& clusterName,std::function<std::string(std::string)> makeURL, const httpRequests::Options& httpOptions){
		std::string credAccessKey, credSecretKey;
		auto url = makeURL("clusters/"+clusterName+"/monitoring_credential");
		auto response=httpRequests::httpGet(url,httpOptions);
		if(response.status==200){ 
			rapidjson::Document json;
			try{
				json.Parse(response.body.c_str());
			}catch(...){
				throw std::runtime_error("Failed to parse JSON data from SLATE API server");
			}
			if(json.IsObject() && json.HasMember("metadata") && json["metadata"].IsObject()
			   && json["metadata"].HasMember("accessKey") && json["metadata"]["accessKey"].IsString()
			   && json["metadata"].HasMember("secretKey") && json["metadata"]["secretKey"].IsString()){
				return std::make_tuple(json["metadata"]["accessKey"].GetString(),
				                       json["metadata"]["secretKey"].GetString());
			}
			else
			throw std::runtime_error("JSON data from SLATE API server did not have the expected structure");
		}
		else if(response.status==500){
			throw std::runtime_error("Failed to allocate a monitoring credential for cluster "+clusterName+
									 ".\nYou should contact the SLATE platform team to ensure that this problem is resolved.");
		}
		throw std::runtime_error("Failed to fetch monitoring credential for cluster "+clusterName+": Error "+std::to_string(response.status));
	}
	
	///Remove the monitoring crediential for a cluster, causing the API server 
	///to treat it as having no monitoring. THis also causes the credential to 
	///be permanently consumed. 
	///\param clusterName the name of the cluster for which to revoke the 
	///                   monitoring credential
	void destroyMonitoringCredential(const std::string& clusterName,std::function<std::string(std::string)> makeURL, const httpRequests::Options& httpOptions){
		std::string credAccessKey, credSecretKey;
		auto url = makeURL("clusters/"+clusterName+"/monitoring_credential");
		auto response=httpRequests::httpDelete(url,httpOptions);
		if(response.status!=200)
			throw std::runtime_error("Failed to remove monitoring credential for cluster "+clusterName+": Error "+std::to_string(response.status));
	}
	
	const std::string monitoringNamespace="slate-monitoring";
	const std::string bucketSecretName="slate-metrics-bucket";
	
	///\pre requires that kubecnfig consistency and helm availability have already been verified
	void installPrometheusImpl(const std::string& configPath, 
	                           const std::string& systemNamespace, 
	                           const std::string& clusterName,
	                           const std::string& credAccessKey, 
	                           const std::string& credSecretKey){
		{ //Create the monitoring namespace, if it does not already exist
			std::cout << "Creating monitoring namespace '" << monitoringNamespace << "'..." << std::endl;
			auto cResult=kubernetes::kubectl(configPath,{"create","namespace",monitoringNamespace});
			if(cResult.status!=0){
				if(cResult.error.find("AlreadyExists")!=std::string::npos)
					std::cout << " (Namespace already exists)" << std::endl;
				else
					throw std::runtime_error("Failed to create namespace: "+cResult.error);
			}
		}
		
		//generate a config file for the target data bucket
		FileHandle bucketConf=makeTemporaryFile(".tmp.bucket.");
		{
			std::ofstream bucketFile(bucketConf);
			//endpoint: "rgw.osiris.org"
			bucketFile << R"(type: S3
config:
  bucket: "slate-metrics"
  endpoint: "192.168.99.100:31311"
  access_key: ")" << credAccessKey << R"("
  insecure: true
  signature_version2: false
  encrypt_sse: false
  secret_key: ")" << credSecretKey << R"("
  put_user_metadata: {}
  http_config:
    idle_conn_timeout: 0s
    insecure_skip_verify: false)";
		}
		{ //Check whether the secret already exists; if so delete it
			auto check=kubernetes::kubectl(configPath,{"get","secret",
				bucketSecretName,"-n",monitoringNamespace,
				"-o=jsonpath={.metadata.name}"});
			if(check.status==0 && check.output==bucketSecretName){
				auto result=kubernetes::kubectl(configPath,{"delete",
					"secret",bucketSecretName,"-n",monitoringNamespace,"--wait"});
				if(result.status!=0)
					throw std::runtime_error("Failed to delete old metrics bucket secret: "+result.error);
			}
		}
		{ //Put the bucket config into a secret
			std::cout << "Storing data bucket configuration as a secret..." << std::endl;
			auto result=kubernetes::kubectl(configPath,{"create","secret","generic",
				bucketSecretName,"-n",monitoringNamespace,
				"--from-file=bucket.yaml="+bucketConf});
			if(result.status!=0)
				throw std::runtime_error("Failed to create bucket secret: "+result.error);
		}
		
		//generate a prometheus/thanos configuration
		FileHandle promValues=makeTemporaryFile(".tmp.prom-values.");
		{
			std::ofstream promFile(promValues);
			//TODO: should grafana default to being enabled?
			promFile << R"(grafana:
  enabled: true
prometheus:
  prometheusSpec:
    image:
      tag: v2.14.0
    externalLabels:
      site: SITE
      cluster: ")" << clusterName << R"("
    thanos:
      objectStorageConfig:
        name: ")" << bucketSecretName << R"("
        key: bucket.yaml)";
		}
		
		{ //check whether the operator is already deployed; if so delete it
			auto listResult=kubernetes::helm(configPath,systemNamespace,{"list",
				"-n",monitoringNamespace,"-o=json"});
			if(listResult.status!=0)
				throw std::runtime_error("helm list failed: "+listResult.error);
			rapidjson::Document json;
			try{
				json.Parse(listResult.output.c_str());
			}catch(...){
				throw std::runtime_error("Failed to parse JSON output from helm list");
			}
			bool alreadyExists=false;
			if(json.IsArray()){
				for(const auto& release : json.GetArray()){
					if(release.IsObject() && release.HasMember("name")
					   && release["name"].IsString() 
					   && release["name"].GetString()==std::string("prometheus-operator"))
					alreadyExists=true;
				}
			}
			if(alreadyExists){
				auto result=kubernetes::helm(configPath,systemNamespace,{"delete",
					"prometheus-operator","-n",monitoringNamespace});
				if(result.status!=0)
					throw std::runtime_error("Failed to delete old prometheus-operator: "+result.error);
			}
		}
		{ //install the prometheus operator
			std::cout << "Installing Prometheus operator with helm..." << std::endl;
			auto result=kubernetes::helm(configPath,systemNamespace,{"install",
				"prometheus-operator","stable/prometheus-operator",
				"--version","8.5.14","--values",promValues,"-n",monitoringNamespace});
			if(result.status!=0)
				throw std::runtime_error("Failed to install Prometheus: "+result.error);
		}
		
		//generate a service manifest to expose thanos
		FileHandle thanosManifest=makeTemporaryFile(".tmp.thanos-manifest.");
		{
			std::ofstream thanosFile(thanosManifest);
			thanosFile << R"(apiVersion: v1
kind: Service
metadata:
  name: thanos-store
  labels:
    thanos: store
spec:
  type: LoadBalancer
  selector:
    app: prometheus
    prometheus: prometheus-operator-prometheus
  ports:
  - protocol: TCP
    port: 10901
    targetPort: 10901)";
		}
		
		{ //create the service
			std::cout << "Creating externally visible Thanos service..." << std::endl;
			auto cResult=kubernetes::kubectl(configPath,{"apply","-f",thanosManifest,
				"--namespace",monitoringNamespace});
			if(cResult.status!=0)
				throw std::runtime_error("Failed to create Thanos service: "+cResult.error);
		}
		
		std::string thanosAddress;
		{ //figure out the address assigned to the Thanos service
			auto result=kubernetes::kubectl(configPath,{"get","services","-n",monitoringNamespace,
				"-l","thanos=store",
				"-o","jsonpath={.items[*].status.loadBalancer.ingress[0].ip}"});
			if(result.status)
				throw std::runtime_error("Failed to check thanos service status: "+result.error);
			if(result.output.empty())
				throw std::runtime_error("The Thanos service has not received an IP address."
										 "This can happen if a LoadBalancer is not installed in the cluster, or has exhausted its pool of allocatable addresses.");
			thanosAddress=result.output;
		}
		//TODO: report the thanos address to the API server
	}
	
	///\pre requires that kubecnfig consistency and helm availability have already been verified
	void removePrometheusImpl(const std::string& configPath, 
							  const std::string& systemNamespace, 
							  const std::string& clusterName){
		{ //destroy the thanos service
			auto result=kubernetes::kubectl(configPath,{"delete",
				"service/thanos-store","-n",monitoringNamespace,"--wait"});
			if(result.status!=0)
				throw std::runtime_error("Failed to delete Thanos service: "+result.error);
		}
		{ //uninstall the prometheus operator
			//helm has no wait option, but our later deletion of the whole 
			//namespace will block if any of its contents are still deleting
			auto result=kubernetes::helm(configPath,systemNamespace,{"delete",
				"prometheus-operator","--namespace",monitoringNamespace});
			if(result.status!=0)
				throw std::runtime_error("Failed to delete Prometheus operator with helm: "+result.error);
		}
		{ //delete the storage credential secret
			auto result=kubernetes::kubectl(configPath,{"delete",
				"secret",bucketSecretName,"-n",monitoringNamespace,"--wait"});
			if(result.status!=0)
				throw std::runtime_error("Failed to delete metrics bucket secret: "+result.error);
		}
		{ //delete the monitoring namespace
			auto result=kubernetes::kubectl(configPath,{"delete",
				"namespace",monitoringNamespace,"--wait"});
			if(result.status!=0)
				throw std::runtime_error("Failed to delete metrics bucket secret: "+result.error);
		}
		//TODO: report removal of the thanos address to the API server
	}
}

void Client::installPrometheusMonitoring(const std::string& configPath, const std::string& systemNamespace) const{
	//helpers to share capabilities with non-mmber functions
	auto makeURL=[this](std::string path){ return(this->makeURL(path)); };
	auto hideProgress=[this](){ return HideProgress(pman_); };
	
	//First find out what cluster we are supposed to be operating on.
	std::string clusterName=getClusterName(configPath,makeURL,defaultOptions(),hideProgress);
	ensureHelmPresent(); //make sure helm is actually available
	
	//Next, get a credential to use for storing the monitoring data. 
	//If we can't get that, we need to abort. 
	std::string credAccessKey, credSecretKey;
	std::tie(credAccessKey, credSecretKey)=fetchMonitoringCredential(clusterName,makeURL,defaultOptions());
	
	installPrometheusImpl(configPath, systemNamespace, clusterName, credAccessKey, credSecretKey);
}

void Client::removePrometheusMonitoring(const std::string& configPath, const std::string& systemNamespace) const{
	//helpers to share capabilities with non-mmber functions
	auto makeURL=[this](std::string path){ return(this->makeURL(path)); };
	auto hideProgress=[this](){ return HideProgress(pman_); };
	
	//First find out what cluster we are supposed to be operating on.
	std::string clusterName=getClusterName(configPath,makeURL,defaultOptions(),hideProgress);
	ensureHelmPresent(); //make sure helm is actually available
	
	removePrometheusImpl(configPath, systemNamespace, clusterName);
	destroyMonitoringCredential(clusterName, makeURL, defaultOptions());
}

void Client::upgradePrometheusMonitoring(const std::string& configPath, const std::string& systemNamespace) const{
	//helpers to share capabilities with non-mmber functions
	auto makeURL=[this](std::string path){ return(this->makeURL(path)); };
	auto hideProgress=[this](){ return HideProgress(pman_); };
	
	//First find out what cluster we are supposed to be operating on.
	std::string clusterName=getClusterName(configPath,makeURL,defaultOptions(),hideProgress);
	ensureHelmPresent(); //make sure helm is actually available
	
	//Next, get a credential to use for storing the monitoring data. 
	//If we can't get that, we need to abort. 
	std::string credAccessKey, credSecretKey;
	std::tie(credAccessKey, credSecretKey)=fetchMonitoringCredential(clusterName,makeURL,defaultOptions());
	
	//we don't have a more clever way to upgrade; we just delete everything and re-install
	removePrometheusImpl(configPath, systemNamespace, clusterName);
	//we do not delete the monitoring credential from the API, since we want to keep using it
	installPrometheusImpl(configPath, systemNamespace, clusterName, credAccessKey, credSecretKey);
}
