#include <client/Client.h>

#include <sstream>

#include <Archive.h>
#include <Process.h>
#include <Utilities.h>

std::string Client::extractClusterConfig(std::string configPath, bool assumeYes){
	const static std::string controllerRepo="https://gitlab.com/ucsd-prp/nrp-controller";
	//const static std::string controllerDeploymentURL="https://gitlab.com/ucsd-prp/nrp-controller/raw/master/deploy.yaml";
	//const static std::string federationRoleURL="https://gitlab.com/ucsd-prp/nrp-controller/raw/master/federation-role.yaml";
	const static std::string controllerDeploymentURL="https://jenkins.slateci.io/artifacts/test/federation-deployment.yaml";
	const static std::string federationRoleURL="https://jenkins.slateci.io/artifacts/test/federation-role.yaml";
	
	//find the config information
	if(configPath.empty()) //try environment
		fetchFromEnvironment("KUBECONFIG",configPath);
	if(configPath.empty()) //try stardard default path
		configPath=getHomeDirectory()+".kube/config";

	if(checkPermissions(configPath)==PermState::DOES_NOT_EXIST)
		throw std::runtime_error("Config file '"+configPath+"' does not exist");
	
	std::cout << "Extracting kubeconfig from " << configPath << "..." << std::endl;
	auto result = runCommand("kubectl",{"config","view","--minify","--flatten","--kubeconfig",configPath});

	if(result.status)
		throw std::runtime_error("Unable to extract kubeconfig: "+result.error);
	//config=result.output;
       
	//Try to figure out whether we are inside of a federation cluster, or 
	//otherwise whether the federation controller is deployed
	std::cout << "Checking for privilege level/deployment controller status..." << std::endl;
	result=runCommand("kubectl",{"get","deployments","-n","kube-system","--kubeconfig",configPath});
	if(result.status==0){
		//We can list objects in kube-system, so permissions are too broad. 
		//Check whether the controller is running:
		if(result.output.find("nrp-controller")==std::string::npos){
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
				HideProgress quiet(pman_);
				std::string answer;
				std::getline(std::cin,answer);
				if(answer!="" && answer!="y" && answer!="Y")
					throw std::runtime_error("Cluster registration aborted");
			}
			else
				std::cout << "assuming yes" << std::endl;
			
			std::cout << "Applying " << controllerDeploymentURL << std::endl;
			result=runCommand("kubectl",{"apply","-f",controllerDeploymentURL,"--kubeconfig",configPath});
			if(result.status)
				throw std::runtime_error("Failed to deploy federation controller: "+result.error);
		}
		else
			std::cout << " Controller is deployed" << std::endl;

		pman_.SetProgress(0.1);

		std::cout << "Ensuring that Custom Resource Definitions are active..." << std::endl;
		while(true){
			result=runCommand("kubectl",{"get","crds"});
			if(result.output.find("clusters.nrp-nautilus.io")!=std::string::npos &&
			   result.output.find("clusternamespaces.nrp-nautilus.io")!=std::string::npos)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		pman_.SetProgress(0.2);
		
		std::cout << "Checking for federation ClusterRole..." << std::endl;
		result=runCommand("kubectl",{"get","clusterrole","federation-cluster","--kubeconfig",configPath});
		if(result.status){
			std::cout << "It appears that the federation-cluster ClusterRole is not deployed on this cluster.\n\n"
			<< "This is a ClusterRole used by the nrp-controller to grant SLATE access\n"
			<< "to only its own namespaces. You can view its definition at\n"
			<< federationRoleURL << ".\n\n"
			<< "This component is needed for SLATE to use this cluster.\n"
			<< "Do you want to install it now? [y]/n: ";
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
			
			std::cout << "Applying " << federationRoleURL << std::endl;
			result=runCommand("kubectl",{"apply","-f",federationRoleURL,"--kubeconfig",configPath});
			if(result.status)
				throw std::runtime_error("Failed to deploy federation clusterrole: "+result.error);
		}
		else
			std::cout << " ClusterRole is defined" << std::endl;
		
		//At this pont we have ensured that we have the right tools, but the 
		//priveleges are still too high. 
		std::cout << "SLATE should be granted access using a ServiceAccount created with a Cluster\n"
		<< "object by the nrp-controller. Do you want to create such a ServiceAccount\n"
		<< "automatically now? [y]/n: ";
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

		std::string namespaceName;
		std::cout << "Please enter the name you would like to give the ServiceAccount and core\n"
		<< "SLATE namespace. The default is 'slate-system': ";
		if(!assumeYes){
			HideProgress quiet(pman_);
			std::cout.flush();
			std::getline(std::cin,namespaceName);
		}
		else
			std::cout << "assuming slate-system" << std::endl;
		if(namespaceName.empty())
			namespaceName="slate-system";
		//check whether the selected namespace/cluster already exists
		result=runCommand("kubectl",{"get","cluster",namespaceName,"-o","name"});
		if(result.status==0 && result.output.find("cluster.nrp-nautilus.io/"+namespaceName)!=std::string::npos){
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
			result=runCommandWithInput("kubectl",
R"(apiVersion: nrp-nautilus.io/v1alpha1
kind: Cluster
metadata: 
  name: )"+namespaceName,
									   {"create","-f","-"});
			if(result.status)
				throw std::runtime_error("Cluster creation failed: "+result.error);
		}
		
		//wait for the corresponding namespace to be ready
		while(true){
			result=runCommand("kubectl",{"get","namespace",namespaceName,"-o","jsonpath={.status.phase}"});
			if(result.status==0 && result.output=="Active")
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		pman_.SetProgress(0.5);
		
		std::cout << "Locating ServiceAccount credentials..." << std::endl;
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
			throw std::runtime_error("Unable to extract ServiceAccount CA data from secret"+result.error);
		std::string caData=result.output;

		pman_.SetProgress(0.7);
		
		std::cout << "Determining server address..." << std::endl;
		result=runCommand("kubectl",{"cluster-info"});
		if(result.status)
			throw std::runtime_error("Unable to get Kubernetes cluster-info: "+result.error);
		std::string serverAddress;
		{
			auto startPos=result.output.find("http");
			if(startPos==std::string::npos)
				throw std::runtime_error("Unable to parse Kubernetes cluster-info");
			auto endPos=result.output.find((char)0x1B,startPos);
			if(endPos==std::string::npos)
				throw std::runtime_error("Unable to parse Kubernetes cluster-info");
			serverAddress=result.output.substr(startPos,endPos-startPos);
		}

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
		
		return os.str();
	}
	else{
		throw std::runtime_error("Unable to list deployments in the kube-system namespace; "
		                         "this command needs to be run with kubernetes administrator "
		                         "privileges in order to create the correct environment (with "
		                         "limited privileges) for SLATE to use.\n"
		                         "Kubernetes error: "+result.error);
	}
}
