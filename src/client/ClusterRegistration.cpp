#include <client/Client.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include <Archive.h>
#include <FileHandle.h>
#include <Process.h>
#include <Utilities.h>

#include <cctype>

void Client::ensureNRPController(const std::string& configPath, bool assumeYes){
	const static std::string expectedControllerVersion="1.2";
	const static std::string controllerRepo="https://gitlab.com/ucsd-prp/nrp-controller";
	//const static std::string controllerDeploymentURL="https://gitlab.com/ucsd-prp/nrp-controller/raw/master/deploy.yaml";
	//const static std::string federationRoleURL="https://gitlab.com/ucsd-prp/nrp-controller/raw/master/federation-role.yaml";
	const static std::string controllerDeploymentURL="https://jenkins.slateci.io/artifacts/test/federation-deployment.yaml";
	
	std::cout << "Checking NRP-controller status..." << std::endl;
	auto result=runCommand("kubectl",{"get","deployments","-n","kube-system","--kubeconfig",configPath});
	if(result.status!=0){
		throw std::runtime_error("Unable to list deployments in the kube-system namespace; "
		                         "this command needs to be run with kubernetes administrator "
		                         "privileges in order to create the correct environment (with "
		                         "limited privileges) for SLATE to use.\n"
		                         "Kubernetes error: "+result.error);
	}
	
	//We can list objects in kube-system, so permissions are too broad. 
	//Check whether the controller is running:
	bool needToInstall=false, deleteExisting=false;
	if(result.output.find("nrp-controller")==std::string::npos)
		needToInstall=true;
	else{
		result=runCommand("kubectl",{"get","pods","-l","k8s-app=nrp-controller","-n","kube-system","-o","jsonpath={.items[*].status.containerStatuses[*].image}"});
		if(result.status!=0){
			throw std::runtime_error("Unable to check image being used by the nrp-controller.\n"
			                         "Kubernetes error: "+result.error);
		}
		std::string installedVersion;
		std::size_t startPos=result.output.rfind(':');
		if(!result.output.empty() && startPos!=std::string::npos && startPos<result.output.size()-1)
			installedVersion=result.output.substr(startPos+1);
		std::cout << "Installed NRP-Controller tag: " << installedVersion << std::endl;
			
		std::string concern;
		if(installedVersion.empty())
			concern="An old version of the nrp-controller is installed; updating it is recommended.";
		else if(installedVersion=="latest"){
			concern="The version of the nrp-controller is unclear; re-installing it is recommended.";
		}
		else if(compareVersions(installedVersion,expectedControllerVersion)==-1){
			concern="An old version of the nrp-controller is installed; updating it is recommended.";
		}
		
		if(!concern.empty()){
			HideProgress quiet(pman_);
			std::cout << concern
			<< "\nDo you want to delete the current version so that a newer one can be "
			<< "installed? [y]/n: ";
			std::cout.flush();
			if(!assumeYes){
				std::string answer;
				std::getline(std::cin,answer);
				if(answer=="" || answer=="y" || answer=="Y")
					deleteExisting=true;
			}
			else{
				std::cout << "assuming yes" << std::endl;
				deleteExisting=true;
			}
		}
	}
	
	if(deleteExisting){
		result=runCommand("kubectl",{"delete","deployments","-l","k8s-app=nrp-controller","-n","kube-system"});
		if(result.status!=0){
			throw std::runtime_error("Unable to remove old NRP Controller deployment.\n"
			                         "Kubernetes error: "+result.error);
		}
		needToInstall=true;
	}
		
	if(needToInstall && !deleteExisting){
		HideProgress quiet(pman_);
		//controller is not deployed, 
		//check whether the user wants us to install it
		std::cout << "It appears that the nrp-controller is not deployed on this cluster.\n\n"
		<< "The nrp-controller is a utility which allows SLATE to operate with\n"
		<< "reduced privileges in your Kubernetes cluster. It grants SLATE access to a\n"
		<< "single initial namespace of your choosing and a mechanism to create additional\n"
		<< "namespaces, without granting it any access to namespaces it has not created.\n"
		<< "This means that you can be certain that SLATE will not interfere with other\n"
		<< "uses of your cluster.\n"
		<< "See " << controllerRepo << " for more information on the\n"
		<< "controller software and \n"
		<< controllerDeploymentURL << " for the\n"
		<< "deployment definition used to install it.\n\n"
		<< "This component is needed for SLATE to use this cluster.\n"
		<< "Do you want to install it now? [y]/n: ";
		std::cout.flush();
		
		if(!assumeYes){
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="" && answer!="y" && answer!="Y")
				throw std::runtime_error("Cluster registration aborted");
		}
		else
			std::cout << "assuming yes" << std::endl;
	}
	
	if(needToInstall){	
		std::cout << "Applying " << controllerDeploymentURL << std::endl;
		result=runCommand("kubectl",{"apply","-f",controllerDeploymentURL,"--kubeconfig",configPath});
		if(result.status)
			throw std::runtime_error("Failed to deploy federation controller: "+result.error);
			
		std::cout << "Waiting for the NRP Controller to become active..." << std::endl;
		while(true){
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			result=runCommand("kubectl",{"get","pods","-n","kube-system","-lk8s-app=nrp-controller","-o","json"});
			if(result.status==0){
				rapidjson::Document resultJSON;
				try{
					resultJSON.Parse(result.output.c_str());
				}catch(...){continue;}
				if(!resultJSON.HasMember("items") || !resultJSON["items"].IsArray() || resultJSON["items"].GetArray().Size()==0)
					continue;
				bool allGood=true, problem=false;
				for(const auto& pod : resultJSON["items"].GetArray()){
					if(!pod.HasMember("status") || !pod["status"].IsObject())
						continue;
					if(!pod["status"].HasMember("phase") || !pod["status"]["phase"].IsString())
						continue;
					std::string status=pod["status"]["phase"].GetString();
					if(status!="Running"){
						allGood=false;
						if(status=="CrashLoopBackoff")
							problem=true;
					}
				}
				if(allGood){
					std::cout << " NRP Controller is active" << std::endl;
					break;
				}
				if(problem)
					throw std::runtime_error("NRP Controller deployment is not healthy; aborting");
			}
		}
	}
	else
		std::cout << " Controller is deployed" << std::endl;
	
	pman_.SetProgress(0.1);

	std::cout << "Ensuring that Custom Resource Definitions are active..." << std::endl;
	while(true){
		result=runCommand("kubectl",{"get","crds"});
		if(result.output.find("clusters.nrp-nautilus.io")!=std::string::npos &&
		   result.output.find("clusternamespaces.nrp-nautilus.io")!=std::string::npos){
		    std::cout << " CRDs are active" << std::endl;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	pman_.SetProgress(0.2);
}



void Client::ensureRBAC(const std::string& configPath, bool assumeYes){
	std::cout << "Checking for federation ClusterRole..." << std::endl;
	auto result=runCommand("kubectl",{"get","clusterrole","federation-cluster","--kubeconfig",configPath});
	if(result.status){
		{
			HideProgress quiet(pman_);
			std::cout << "It appears that the federation-cluster ClusterRole is not deployed on this cluster.\n\n"
			<< "This is a ClusterRole used by the nrp-controller to grant SLATE access\n"
			<< "to only its own namespaces. You can view its definition at\n"
			<< federationRoleURL << ".\n\n"
			<< "This component is needed for SLATE to use this cluster.\n"
			<< "Do you want to install it now? [y]/n: ";
			std::cout.flush();
			
			if(!assumeYes){
				std::string answer;
				std::getline(std::cin,answer);
				if(answer!="" && answer!="y" && answer!="Y")
					throw std::runtime_error("Cluster registration aborted");
			}
			else
				std::cout << "assuming yes" << std::endl;
		}
		
		std::cout << "Applying " << federationRoleURL << std::endl;
		result=runCommand("kubectl",{"apply","-f",federationRoleURL,"--kubeconfig",configPath});
		if(result.status)
			throw std::runtime_error("Failed to deploy federation clusterrole: "+result.error);
	}
	else
		std::cout << " ClusterRole is defined" << std::endl;

	pman_.SetProgress(0.3);
}

///\pre configPath must be a known-good path to a kubeconfig
///\return Whether MetalLB was found to be running _or_ it was not but the user 
///        claimed that there is some LoadBalancer present
bool Client::checkLoadBalancer(const std::string& configPath, bool assumeYes){
	std::cout << "Checking for a LoadBalancer..." << std::endl;
	auto result=runCommand("kubectl",{"get","pods","--all-namespaces",
	                                  "-l","app=metallb,component=controller",
	                                  "-o","jsonpath={.items[*].status.phase}",
	                                  "--kubeconfig",configPath});
	bool present;
	if(result.status)
		present=false;
	else
		present=(result.output.find("Running")!=std::string::npos);
	
	if(!present){
		HideProgress quiet(pman_);
		std::cout << "Unable to detect a (MetalLB) load balancer.\n"
		<< "SLATE requires a load balancer for its ingress controller.\n"
		<< "Does this cluster have a load balancer which has not been correctly detected? [y]/n: ";
		//Do _not_ allow the usual assumeYes logic here because if we failed to 
		//auto-detect we need the user to make a choice which doesn't really 
		//have any default
		//if(!assumeYes){
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="" && answer!="y" && answer!="Y")
				present=false;
			else
				present=true;
		/*}
		else{
			std::cout << "assuming yes" << std::endl;
			present=true;
		}*/
	}
	else{
		std::cout << " Found MetalLB" << std::endl;
	}
	return present;
}

std::string Client::getIngressControllerAddress(const std::string& configPath, const std::string& systemNamespace) const{
	std::cout << "Finding the LoadBalancer address assigned to the ingress controller..." << std::endl;
	unsigned int attempts=0;
	do{
		if(attempts)
			std::this_thread::sleep_for(std::chrono::seconds(5));
		attempts++;
		auto result=runCommand("kubectl",{"get","services","-n",systemNamespace,
										  "-l","app.kubernetes.io/name=ingress-nginx",
										  "-o","jsonpath={.items[*].status.loadBalancer.ingress[0].ip}",
										  "--kubeconfig",configPath});
		if(result.status)
			throw std::runtime_error("Failed to check ingress controller service status: "+result.error);
		if(!result.output.empty())
			return result.output;
	}while(attempts<25); //keep trying for up to two minutes
	throw std::runtime_error("Ingress controller service has not received an IP address."
		"This can happen if a LoadBalancer is not installed in the cluster, "
		"or has exhausted its pool of allocatable addresses.");
}

///\pre configPath must be a known-good path to a kubeconfig
///\param systemNamespace the SLATE system namespace
void Client::ensureIngressController(const std::string& configPath, const std::string& systemNamespace, bool assumeYes) const{
	std::cout << "Checking for a SLATE ingress controller..." << std::endl;

	bool installed=checkIngressController(configPath,systemNamespace)!=ClusterComponent::NotInstalled;
	if(installed){
		std::cout << " Found a running ingress controller" << std::endl;
		return;
	}

	{
		HideProgress quiet(pman_);
		std::cout << "SLATE requires an ingress controller to support user-friendly DNS names for HTTP\n"
		<< "services. SLATE's controller uses a customized ingress class so that it should\n"
		<< "not conflict with other controllers.\n"
		<< "Do you want to install the ingress controller now? [y]/n: ";
				std::cout.flush();
		if(!assumeYes){
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="" && answer!="y" && answer!="Y")
				throw InstallAborted("Ingress controller installation aborted");
		}
		else
			std::cout << "assuming yes" << std::endl;
	}
	
	installIngressController(configPath,systemNamespace);
}

Client::ClusterConfig Client::extractClusterConfig(std::string configPath, bool assumeYes){
	
	std::string namespaceName;
	{
		HideProgress quiet(pman_);
		std::cout << "SLATE should be granted access using a ServiceAccount created with a Cluster\n"
		<< "object by the nrp-controller. Do you want to create such a ServiceAccount\n"
		<< "automatically now? [y]/n: ";
		std::cout.flush();
		if(!assumeYes){
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="" && answer!="y" && answer!="Y")
				throw std::runtime_error("Cluster registration aborted");
		}
		else
			std::cout << "assuming yes" << std::endl;

		std::cout << "Please enter the name you would like to give the ServiceAccount and core\n"
		<< "SLATE namespace. The default is '" << defaultSystemNamespace << "': ";
		if(!assumeYes){
			std::cout.flush();
			std::getline(std::cin,namespaceName);
		}
		else
			std::cout << "assuming " << defaultSystemNamespace << std::endl;
	}
	
	if(namespaceName.empty())
		namespaceName=defaultSystemNamespace;
	//check whether the selected namespace/cluster already exists
	auto result=runCommand("kubectl",{"get","cluster",namespaceName,"-o","name"});
	if(result.status==0 && result.output.find("cluster.nrp-nautilus.io/"+namespaceName)!=std::string::npos){
		HideProgress quiet(pman_);
		std::cout << "The namespace '" << namespaceName << "' already exists.\n"
		<< "Proceed with reusing it? [y]/n: ";
		std::cout.flush();
		if(!assumeYes){
			HideProgress quiet(pman_);
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="" && answer!="y" && answer!="Y")
				throw std::runtime_error("Cluster registration aborted");
		}
		else
			std::cout << "assuming yes" << std::endl;
	}
	else{
		std::cout << "Creating Cluster '" << namespaceName << "'..." << std::endl;
		FileHandle clusterFile=makeTemporaryFile(".cluster.yaml.");
		std::ofstream clusterYaml(clusterFile);
		clusterYaml << 
R"(apiVersion: nrp-nautilus.io/v1alpha1
kind: Cluster
metadata: 
  name: )" << namespaceName << std::endl;
		result=runCommand("kubectl",{"create","-f",clusterFile});
		if(result.status)
			throw std::runtime_error("Cluster creation failed: "+result.error);
	}

	//Tricky point: if the namespace name is already in use, the nrp-controller
	//pseudo-helpfully makes up a different one. First we need to detect if this
	//has happened. 
	unsigned int attempts=0;
	do{
		if(attempts)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		result=runCommand("kubectl",{"get","cluster.nrp-nautilus.io",namespaceName,"-o","jsonpath={.spec.Namespace}"});
	}
	while((result.status || result.output.empty()) && attempts++<10);
	if(result.status || result.output.empty())
		throw std::runtime_error("Checking created namespace name failed: "+result.error);
	if(namespaceName!=result.output){
		std::ostringstream ss;
		ss << "Created namespace name does not match Cluster object name: \n"
		<< "  Selected cluster object name: " << namespaceName << '\n'
		<< "  Resulting namespace name: " << result.output << '\n'
		<< "This typically happens when a " << namespaceName << " namespace\n"
		<< "already exists (possibly in a Terminating state). \n"
		<< "`kubectl describe namespace " << namespaceName << "` can be used\n"
		<< "to investigate this before running `slate cluster create` again.";
		
		result=runCommand("kubectl",{"delete","cluster.nrp-nautilus.io",namespaceName});
		
		if(result.status){
			ss << "\nFailed to delete cluster " << namespaceName << ":\n"
			<< result.error << "\nManual deletion is recommended";
		}
		
		throw std::runtime_error(ss.str());
	}
	namespaceName=result.output;
	
	//wait for the corresponding namespace to be ready
	{
		HideProgress quiet(pman_);
		std::cout << "Waiting for namespace " << namespaceName << " to become ready..." << std::endl;
	}
	attempts=0;
	while(true){
		result=runCommand("kubectl",{"get","namespace",namespaceName,"-o","jsonpath={.status.phase}"});
		if(result.status==0 && result.output=="Active")
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if(++attempts == 600){
			HideProgress quiet(pman_);
			std::cout << "Namespace creation is taking abnormally long.\n"
			<< "If progress does not occur shortly, you may want to abort this process (Ctrl+C)\n"
			<< "and either examine the state of the " << namespaceName << " namespace or run\n"
			<< "`kubectl delete cluster.nrp-nautilus.io " << namespaceName << "` before running\n"
			<< "this command again." << std::endl;
		}
	}

	pman_.SetProgress(0.5);
	
	//wait for the corresponding service account to be ready
	{
		HideProgress quiet(pman_);
		std::cout << "Locating ServiceAccount credentials..." << std::endl;
	}
	attempts=0;
	while(true){
		result=runCommand("kubectl",{"get","serviceaccount",namespaceName,"-n",namespaceName,"-o","jsonpath='{.secrets[].name}'"});
		if(result.status==0 && !result.output.empty())
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if(++attempts == 600){
			HideProgress quiet(pman_);
			std::cout << "ServiceAccount creation is taking abnormally long.\n"
			<< "If progress does not occur shortly, you may want to abort this process (Ctrl+C)\n"
			<< "and either examine the state of the " << namespaceName << " namespace and serviceaccount\n"
			<< " or run `kubectl delete cluster.nrp-nautilus.io " << namespaceName << "` before running\n"
			<< "this command again." << std::endl;
		}
	}
	result=runCommand("kubectl",{"get","serviceaccount",namespaceName,"-n",namespaceName,"-o","jsonpath='{.secrets[].name}'"});
	if(result.status)
		throw std::runtime_error("Unable to locate ServiceAccount credential secret: "+result.error);
	std::string credName=result.output;
	{ //kubectl leaves quotes around the name. Get rid of them.
		std::size_t pos;
		while((pos=credName.find('\''))!=std::string::npos)
			credName.erase(pos,1);
	}

	pman_.SetProgress(0.6);
	
	std::cout << "Extracting CA data..." << std::endl;
	result=runCommand("kubectl",{"get","secret",credName,"-n",namespaceName,"-o","jsonpath='{.data.ca\\.crt}'"});
	if(result.status)
		throw std::runtime_error("Unable to extract ServiceAccount CA data from secret "+result.error);
	std::string caData=result.output;

	pman_.SetProgress(0.7);
	
	std::cout << "Determining server address..." << std::endl;
	result=runCommand("kubectl",{"cluster-info"});
	if(result.status)
		throw std::runtime_error("Unable to get Kubernetes cluster-info: "+result.error);
	std::string serverAddress;
	bool serverAddressGood=false;
	std::string badnessReason;
	{
		std::string cleanOutput=removeShellEscapeSequences(result.output);
		static const std::string label="Kubernetes master is running at ";
		auto pos=cleanOutput.find(label);
		if(pos==std::string::npos)
			badnessReason="Unable to find expected label in kubectl output";
		else{
			if(pos+label.size()>=cleanOutput.size())
				badnessReason="Did not find enough data in kubectl output";
			pos+=label.size();
			auto end=cleanOutput.find('\n',pos);
			serverAddress=cleanOutput.substr(pos,end==std::string::npos?end:end-pos);
			if(serverAddress.empty())
				badnessReason="Extracted kubernetes API server address is empty";
			else
				serverAddressGood=true;
		}
	}
	do{
		if(!serverAddressGood){
			HideProgress quiet(pman_);
			std::cout << "The entered/detected Kubernetes API server address,\n"
			<< '"' << serverAddress << "\"\n"
			<<"has a ";
			if(!serverAddress.empty())
				std::cout << "possible ";
			std::cout << "problem:\n" << badnessReason << std::endl;
			std::cout << "Public cluster URL";
			if(!serverAddress.empty())
				std::cout << " or continue with existing? [" << serverAddress << ']';
			std::cout << ": ";
			std::cout.flush();
			std::string answer;
			std::getline(std::cin,answer);
			if(answer.empty() && !serverAddress.empty()){
				std::cout << "Continuing with " << serverAddress << std::endl;
				serverAddressGood=true;
				break;
			}
			serverAddress=answer;
			serverAddressGood=true; //optimism!
		}
		if(!serverAddress.empty()){
			//check whether the address appears vaguely like a URL
			if(serverAddress.find("http")!=0){
				serverAddressGood=false;
				badnessReason="Server address does not appear to be a valid http(s) URL";
			}
			//check whether the host is an RFC 1918 private address
			#ifdef SLATE_EXTRACT_HOSTNAME_AVAIL
			std::string host=httpRequests::extractHostname(serverAddress);
			if(host.find("10.")==0){
				serverAddressGood=false;
				badnessReason="Host address appears to be in the 10.0.0.0/8 private CIDR block";
			}
			for(unsigned int o2=16; o2<32; o2++){
				std::string prefix="172."+std::to_string(o2)+".";
				if(host.find(prefix)==0){
					serverAddressGood=false;
					badnessReason="Host address appears to be in the 172.16.0.0/12 private CIDR block";
				}
			}
			if(host.find("192.168.")==0){
				serverAddressGood=false;
				badnessReason="Host address appears to be in the 192.168.0.0/16 private CIDR block";
			}
			#else //old curl version, try something dumber and more error-prone
			if(serverAddress.find("//10.")!=std::string::npos){
				serverAddressGood=false;
				badnessReason="Host address appears to be in the 10.0.0.0/8 private CIDR block";
			}
			for(unsigned int o2=16; o2<32; o2++){
				std::string prefix="//172."+std::to_string(o2)+".";
				if(serverAddress.find(prefix)!=std::string::npos){
					serverAddressGood=false;
					badnessReason="Host address appears to be in the 172.16.0.0/12 private CIDR block";
				}
			}
			if(serverAddress.find("//192.168.")!=std::string::npos){
				serverAddressGood=false;
				badnessReason="Host address appears to be in the 192.168.0.0/16 private CIDR block";
			}
			#endif
		}
	}while(!serverAddressGood);

	pman_.SetProgress(0.8);
	
	std::cout << "Extracting ServiceAccount token..." << std::endl;
	result=runCommand("kubectl",{"get","secret","-n",namespaceName,credName,"-o","jsonpath={.data.token}"});
	if(result.status)
		throw std::runtime_error("Unable to extract ServiceAccount token data from secret: "+result.error);
	std::string token=decodeBase64(result.output);
	
	std::ostringstream os;
	os << R"(apiVersion: v1
clusters:
- cluster:
    certificate-authority-data: )"
	<< caData << '\n'
	<< "    server: " << serverAddress << '\n'
	<< R"(  name: )" << namespaceName << R"(
contexts:
- context:
    cluster: )" << namespaceName << R"(
    namespace: )" << namespaceName << '\n'
	<< "    user: " << namespaceName << '\n'
	<< R"(  name: )" << namespaceName << R"(
current-context: )" << namespaceName << R"(
kind: Config
preferences: {}
users:
- name: )" << namespaceName << '\n'
	<< R"(  user:
    token: )" << token << '\n';
	std::cout << " Done generating config with limited privileges" << std::endl;
	
	return {namespaceName,os.str()};	
}
