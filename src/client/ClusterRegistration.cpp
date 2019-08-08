#include <client/Client.h>

#include <iostream>
#include <sstream>

#include <Archive.h>
#include <Process.h>
#include <Utilities.h>

#include <cctype>

///Compare version strings in the same manner as rpmvercmp, as described by
///https://blog.jasonantman.com/2014/07/how-yum-and-rpm-compare-versions/#how-rpm-compares-version-parts
///retrieved 20190702
///\return -1 if a represents an older version than b
///         0 if a and b represent the same version
///         1 if a represents a newer version than b
int compareVersions(const std::string& a, const std::string& b){
	//1: If the strings are. . .  equal, return 0.
	if(a==b)
		return 0;
	//use xpos as the beginning of the part of string x remaining to be considered
	std::size_t apos=0, bpos=0, aseg, bseg;
	//2: Loop over the strings, left-to-right.
	while(apos<a.size() && bpos<b.size()){
		//2.1: Trim anything that’s not [A-Za-z0-9] or tilde (~) from the front 
		//     of both strings.
		while(apos<a.size() && !(isalnum(a[apos]) || a[apos]=='~'))
			apos++;
		while(bpos<b.size() && !(isalnum(b[bpos]) || b[bpos]=='~'))
			bpos++;
		if(apos==std::string::npos || bpos==std::string::npos)
			break;
		//2.2: If both strings start with a tilde, discard it and move on to the 
		//     next character.
		if(a[apos]=='~' && b[bpos]=='~'){
			apos++;
			bpos++;
			//2.4: End the loop if either string has reached zero length.
			if(apos==a.size() || bpos==b.size())
				break;
		}
		//2.3: If string a starts with a tilde and string b does not, return -1 
		//     (string a is older); and the inverse if string b starts with a 
		//     tilde and string a does not.
		else if(a[apos]=='~')
			return -1;
		else if(b[bpos]=='~')
			return 1;
		//2.5: If the first character of a is a digit, pop the leading chunk of 
		//     continuous digits from each string
		//(define the 'popped' segment as characters [xseg,xpos) for string x)
		if(isdigit(a[apos])){
			aseg=apos++; //increment apos because we know it was a digit
			while(apos<a.size() && isdigit(a[apos]))
				apos++;
			bseg=bpos; //bpos might not be a digit, do not increment
			while(bpos<b.size() && isdigit(b[bpos]))
				bpos++;
		}
		//2.5 cont'd: If a begins with a letter, do the same for leading letters.
		if(isalpha(a[apos])){
			aseg=apos++; //increment apos because we know it was a letter
			while(apos<a.size() && isalpha(a[apos]))
				apos++;
			bseg=bpos; //bpos might not be a letter, do not increment
			while(bpos<b.size() && isalpha(b[bpos]))
				bpos++;
		}
		//2.6: If the segement from b had 0 length, return 1 if the segment from 
		//     a was numeric, or -1 if it was alphabetic. 
		if(bpos==bseg){
			if(isdigit(a[aseg]))
				return 1;
			if(isalpha(a[aseg]))
				return -1;
		}
		//2.7: If the leading segments were both numeric, discard any leading 
		//     zeros. If a is longer than b (without leading zeroes), return 1, 
		//     and vice-versa.
		if(isdigit(a[aseg])){
			while(aseg<apos && a[aseg]=='0') //trim a
				aseg++;
			while(bseg<bpos && b[bseg]=='0') //trim b
				bseg++;
			if(apos-aseg > bpos-bseg) //a is longer than b
				return 1;
			if(bpos-bseg > apos-aseg) //b is longer than a
				return -1;
		}
		//2.8: Compare the leading segments with strcmp(). If that returns a 
		//non-zero value, then return that value.
		//(Implement the equivalent of strcmp inline because segments are 
		//not null terminated.)
		while(aseg<apos && bseg<bpos){
			if(a[aseg]<b[bseg])
				return -1;
			if(a[aseg]>b[bseg])
				return 1;
			aseg++;
			bseg++;
		}
		if(aseg==apos && bseg<bpos)
			return -1;
		if(bseg==bpos && aseg<apos)
			return 1;
	}
	//If the loop ended then the longest wins - if what’s left of a is longer 
	//than what’s left of b, return 1. Vice-versa for if what’s left of b is 
	//longer than what’s left of a. And finally, if what’s left of them is the 
	//same length, return 0.
	if(a.size()-apos > b.size()-bpos)
		return 1;
	if(a.size()-apos < b.size()-bpos)
		return -1;
	return 0;
}

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
			std::cout << concern
			<< "\nDo you want to delete the current version so that a newer one can be "
			<< "installed? [y]/n: ";
			std::cout.flush();
			if(!assumeYes){
				HideProgress quiet(pman_);
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
	}
	
	if(needToInstall){	
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
		   result.output.find("clusternamespaces.nrp-nautilus.io")!=std::string::npos){
		    std::cout << " CRDs are active" << std::endl;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	pman_.SetProgress(0.2);
}

void Client::ensureRBAC(const std::string& configPath, bool assumeYes){
	const static std::string federationRoleURL="https://jenkins.slateci.io/artifacts/test/federation-role.yaml";
	
	std::cout << "Checking for federation ClusterRole..." << std::endl;
	auto result=runCommand("kubectl",{"get","clusterrole","federation-cluster","--kubeconfig",configPath});
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
		std::cout << "Unable to detect a (MetalLB) load balancer.\n"
		<< "SLATE requires a load balancer for its ingress controller.\n"
		<< "Does this cluster have a load balancer which has not been correctly detected? [y]/n: ";
		//Do _not_ allow the usual assumeYes logic here because if we failed to 
		//auto-detect we need the user to make a choice which doesn't really 
		//have any default
		//if(!assumeYes){
			HideProgress quiet(pman_);
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

const static std::string namespacePlaceholder="{{SLATE_NAMESPACE}}";
const static std::string componentVersionPlaceholder="{{COMPONENT_VERSION}}";

const static std::string ingressControllerVersion="v1";
const static std::string ingressControllerConfig=
R"(# Based on https://raw.githubusercontent.com/kubernetes/ingress-nginx/master/deploy/mandatory.yaml
# as of commit e0793650d08d17dbff44755a56ae9ab7c8ab6a21
kind: ConfigMap
apiVersion: v1
metadata:
  name: nginx-configuration
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    slate-ingress-version: {{COMPONENT_VERSION}}

---
kind: ConfigMap
apiVersion: v1
metadata:
  name: tcp-services
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    slate-ingress-version: {{COMPONENT_VERSION}}

---
kind: ConfigMap
apiVersion: v1
metadata:
  name: udp-services
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    slate-ingress-version: {{COMPONENT_VERSION}}

---
apiVersion: v1
kind: ServiceAccount
metadata:
  name: slate-nginx-ingress-serviceaccount
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    slate-ingress-version: {{COMPONENT_VERSION}}

---
apiVersion: rbac.authorization.k8s.io/v1beta1
kind: ClusterRole
metadata:
  name: slate-nginx-ingress-clusterrole
  labels:
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    slate-ingress-version: {{COMPONENT_VERSION}}
rules:
 - apiGroups:
     - ""
   resources:
     - configmaps
     - endpoints
     - nodes
     - pods
     - secrets
   verbs:
     - list
     - watch
 - apiGroups:
     - ""
   resources:
     - nodes
   verbs:
     - get
 - apiGroups:
     - ""
   resources:
     - services
   verbs:
     - get
     - list
     - watch
 - apiGroups:
     - "extensions"
   resources:
     - ingresses
   verbs:
     - get
     - list
     - watch
 - apiGroups:
     - ""
   resources:
     - events
   verbs:
     - create
     - patch
 - apiGroups:
     - "extensions"
   resources:
     - ingresses/status
   verbs:
     - update

---
apiVersion: rbac.authorization.k8s.io/v1beta1
kind: Role
metadata:
 name: slate-nginx-ingress-role
 namespace: {{SLATE_NAMESPACE}}
 labels:
   app.kubernetes.io/name: ingress-nginx
   app.kubernetes.io/part-of: ingress-nginx
   slate-ingress-version: {{COMPONENT_VERSION}}
rules:
 - apiGroups:
     - ""
   resources:
     - configmaps
     - pods
     - secrets
     - namespaces
   verbs:
     - get
 - apiGroups:
     - ""
   resources:
     - configmaps
   resourceNames:
     # Defaults to "<election-id>-<ingress-class>"
     # Here: "<ingress-controller-leader>-<nginx>"
     # This has to be adapted if you change either parameter
     # when launching the nginx-ingress-controller.
     - "ingress-controller-leader-nginx"
   verbs:
     - get
     - update
 - apiGroups:
     - ""
   resources:
     - configmaps
   verbs:
     - create
 - apiGroups:
     - ""
   resources:
     - endpoints
   verbs:
     - get

---
apiVersion: rbac.authorization.k8s.io/v1beta1
kind: RoleBinding
metadata:
 name: slate-nginx-ingress-role-nisa-binding
 namespace: {{SLATE_NAMESPACE}}
 labels:
   app.kubernetes.io/name: ingress-nginx
   app.kubernetes.io/part-of: ingress-nginx
   slate-ingress-version: {{COMPONENT_VERSION}}
roleRef:
 apiGroup: rbac.authorization.k8s.io
 kind: Role
 name: slate-nginx-ingress-role
subjects:
 - kind: ServiceAccount
   name: slate-nginx-ingress-serviceaccount
   namespace: {{SLATE_NAMESPACE}}

---
apiVersion: rbac.authorization.k8s.io/v1beta1
kind: ClusterRoleBinding
metadata:
 name: slate-nginx-ingress-clusterrole-nisa-binding
 labels:
   app.kubernetes.io/name: ingress-nginx
   app.kubernetes.io/part-of: ingress-nginx
   slate-ingress-version: {{COMPONENT_VERSION}}
roleRef:
 apiGroup: rbac.authorization.k8s.io
 kind: ClusterRole
 name: slate-nginx-ingress-clusterrole
subjects:
 - kind: ServiceAccount
   name: slate-nginx-ingress-serviceaccount
   namespace: {{SLATE_NAMESPACE}}

---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: nginx-ingress-controller
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    slate-ingress-version: {{COMPONENT_VERSION}}
spec:
  replicas: 1
  selector:
    matchLabels:
      app.kubernetes.io/name: ingress-nginx
      app.kubernetes.io/part-of: ingress-nginx
  template:
    metadata:
      labels:
        app.kubernetes.io/name: ingress-nginx
        app.kubernetes.io/part-of: ingress-nginx
        slate-ingress-version: {{COMPONENT_VERSION}}
      annotations:
        prometheus.io/port: "10254"
        prometheus.io/scrape: "true"
    spec:
      serviceAccountName: slate-nginx-ingress-serviceaccount
      containers:
        - name: nginx-ingress-controller
          image: quay.io/kubernetes-ingress-controller/nginx-ingress-controller:0.23.0
          args:
            - /nginx-ingress-controller
            - --configmap=$(POD_NAMESPACE)/nginx-configuration
            - --ingress-class=slate
            - --tcp-services-configmap=$(POD_NAMESPACE)/tcp-services
            - --udp-services-configmap=$(POD_NAMESPACE)/udp-services
            - --publish-service=$(POD_NAMESPACE)/ingress-nginx
            - --annotations-prefix=nginx.ingress.kubernetes.io
          securityContext:
            allowPrivilegeEscalation: true
            capabilities:
              drop:
                - ALL
              add:
                - NET_BIND_SERVICE
            # www-data -> 33
            runAsUser: 33
          env:
            - name: POD_NAME
              valueFrom:
                fieldRef:
                  fieldPath: metadata.name
            - name: POD_NAMESPACE
              valueFrom:
                fieldRef:
                  fieldPath: metadata.namespace
          ports:
            - name: http
              containerPort: 80
            - name: https
              containerPort: 443
          livenessProbe:
            failureThreshold: 3
            httpGet:
              path: /healthz
              port: 10254
              scheme: HTTP
            initialDelaySeconds: 10
            periodSeconds: 10
            successThreshold: 1
            timeoutSeconds: 10
          readinessProbe:
            failureThreshold: 3
            httpGet:
              path: /healthz
              port: 10254
              scheme: HTTP
            periodSeconds: 10
            successThreshold: 1
            timeoutSeconds: 10

---
apiVersion: v1
kind: Service
metadata:
  name: ingress-nginx
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    slate-ingress-version: {{COMPONENT_VERSION}}
spec:
  type: LoadBalancer
  #type: NodePort
  ports:
    - name: http
      port: 80
      targetPort: 80
      protocol: TCP
    - name: https
      port: 443
      targetPort: 443
      protocol: TCP
  selector:
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    slate-ingress-version: {{COMPONENT_VERSION}}

---
)";

Client::ClusterComponent::ComponentStatus Client::checkIngressController(const std::string& configPath, const std::string& systemNamespace) const{
	auto result=runCommand("kubectl",{"get","pods","-n",systemNamespace,
	                                  "-l=slate-ingress-version",
	                                  //"-o=jsonpath={.items[*].status.phase}",
	                                  "-o=json",
	                                  "--kubeconfig",configPath});
	if(result.status!=0)
		throw std::runtime_error("kubectl failed: "+result.error);
	
	rapidjson::Document json;
	json.Parse(result.output.c_str());	
	
	if(!json.HasMember("items") || !json["items"].IsArray())
		throw std::runtime_error("Malformed JSON from kubectl");
	if(json["items"].Size()==0) //if no pods, the controller is not installed
		return ClusterComponent::NotInstalled;
		
	//TODO: find and compare version
	if(!json["items"][0].IsObject() || !json["items"][0].HasMember("metadata") || !json["items"][0]["metadata"].IsObject())
		throw std::runtime_error("Malformed JSON from kubectl");
	
	if(json["items"][0]["metadata"].HasMember("labels") && json["items"][0]["metadata"].IsObject() &&
	  json["items"][0]["metadata"]["labels"].HasMember("slate-ingress-version")){
		std::string installedVersion=json["items"][0]["metadata"]["labels"]["slate-ingress-version"].GetString();
		int verComp=compareVersions(installedVersion,ingressControllerVersion);
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
	
	/*bool ready=result.output.find("Running")!=std::string::npos;
	if(ready) //TODO: check version
		return ClusterComponent::UpToDate;
	if(!result.output.empty())
		throw std::runtime_error("SLATE ingress controller is installed but in an unexpected state: "+result.output);
	return ClusterComponent::NotInstalled;*/
}

void Client::installIngressController(const std::string& configPath, const std::string& systemNamespace) const{
	ProgressToken progress(pman_,"Installing ingress controller");
	std::string ingressControllerConfig=::ingressControllerConfig;
	//replace all namespace placeholders
	std::size_t pos;
	while((pos=ingressControllerConfig.find(namespacePlaceholder))!=std::string::npos)
		ingressControllerConfig.replace(pos,namespacePlaceholder.size(),systemNamespace);
	while((pos=ingressControllerConfig.find(componentVersionPlaceholder))!=std::string::npos)
		ingressControllerConfig.replace(pos,componentVersionPlaceholder.size(),ingressControllerVersion);
		
	auto result=runCommandWithInput("kubectl",ingressControllerConfig,{"apply","--kubeconfig",configPath,"-f","-"});
	if(result.status)
		throw std::runtime_error("Failed to install ingress controller: "+result.error);
}

namespace kubernetes{
std::multimap<std::string,std::string> findAll(const std::string& clusterConfig, const std::string& selector, const std::string nspace, const std::string verbs){
	std::multimap<std::string,std::string> objects;

	//first determine all possible API resource types
	auto result=runCommand("kubectl", {"--kubeconfig="+clusterConfig,"api-resources","-o=name","--verbs="+verbs});
	if(result.status!=0)
		throw std::runtime_error("Failed to determine list of Kubernetes resource types");
	std::vector<std::string> resourceTypes;
	std::istringstream ss(result.output);
	std::string item;
	while(std::getline(ss,item))
		resourceTypes.push_back(item);
	
	//for every type try to find every object matching the selector
	std::vector<std::string> baseArgs={"get","--kubeconfig="+clusterConfig,"-o=jsonpath={.items[*].metadata.name}","-l="+selector};
	if(!nspace.empty())
		baseArgs.push_back("-n="+nspace);
	for(const auto& type : resourceTypes){
		auto args=baseArgs;
		args.insert(args.begin()+1,type);
		result=runCommand("kubectl", args);
		if(result.status!=0)
			throw std::runtime_error("Failed to list resources of type "+type);
		ss.str(result.output);
		ss.clear();
		while(ss >> item)
			objects.emplace(type,item);
	}
	return objects;
}
}

void Client::removeIngressController(const std::string& configPath, const std::string& systemNamespace) const{
	ProgressToken progress(pman_,"Removing ingress controller");
	auto objects=kubernetes::findAll(configPath,"slate-ingress-version",systemNamespace,"get,delete");
	std::vector<std::string> deleteArgs={"delete","--kubeconfig="+configPath,"-n="+systemNamespace,"--ignore-not-found"};
	deleteArgs.reserve(deleteArgs.size()+objects.size());
	for(const auto& object : objects)
		deleteArgs.push_back(object.first+"/"+object.second);
	auto result=runCommand("kubectl",deleteArgs);
	if(result.status!=0)
		throw std::runtime_error("Failed to remove ingress controller: "+result.error);
}

void Client::upgradeIngressController(const std::string& configPath, const std::string& systemNamespace) const{
	try{
		auto status=checkIngressController(configPath,systemNamespace);
		if(status==ClusterComponent::OutOfDate)
			removeIngressController(configPath,systemNamespace);
		if(status!=ClusterComponent::UpToDate)
			installIngressController(configPath,systemNamespace);
		else
			std::cout << "Nothing to do" << std::endl;
	}
	catch(std::runtime_error& err){
		throw std::runtime_error("Failed to upgrade ingress controller: "+std::string(err.what()));
	}
}

std::string Client::getIngressControllerAddress(const std::string& configPath, const std::string& systemNamespace) const{
	auto result=runCommand("kubectl",{"get","services","-n",systemNamespace,
	                                  "-l","app.kubernetes.io/name=ingress-nginx",
	                                  "-o","jsonpath={.items[*].status.loadBalancer.ingress[0].ip}",
	                                  "--kubeconfig",configPath});
	if(result.status)
		throw std::runtime_error("Failed to check ingress controller service status: "+result.error);
	if(result.output.empty())
		throw std::runtime_error("Ingress controller service has not received an IP address."
		"This can happen if a LoadBalancer is not installed in the cluster, or has exhausted its pool of allocatable addresses.");
	return result.output;
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

	std::cout << "SLATE requires an ingress controller to support user-friendly DNS names for HTTP\n"
	<< "services. SLATE's controller uses a customized ingress class so that it should\n"
	<< "not conflict with other controllers.\n"
	<< "Do you want to install the ingress controller now? [y]/n: ";
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
	
	installIngressController(configPath,systemNamespace);
}

Client::ClusterConfig Client::extractClusterConfig(std::string configPath, bool assumeYes){
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
	auto result=runCommand("kubectl",{"get","cluster",namespaceName,"-o","name"});
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
		throw std::runtime_error("Unable to extract ServiceAccount CA data from secret "+result.error);
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
	
	return {namespaceName,os.str()};	
}
