#include <client/Client.h>

#include <iostream>

#include "KubeInterface.h"
#include <Utilities.h>

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
     - "ingress-controller-leader-slate"
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
	auto result=runCommand("kubectl",{"get","deployments","-n",systemNamespace,
	                                  "-l=slate-ingress-version",
	                                  "-o=json",
	                                  "--kubeconfig",configPath});
	if(result.status!=0)
		throw std::runtime_error("kubectl failed: "+result.error);
	
	rapidjson::Document json;
	json.Parse(result.output.c_str());
	
	if(!json.HasMember("items") || !json["items"].IsArray())
		throw std::runtime_error("Malformed JSON from kubectl");
	if(json["items"].Size()==0){ //found nothing
		//try looking for a version too old to have the version label
		result=runCommand("kubectl",{"get","deployments","-n",systemNamespace,
	                                  "-l=app.kubernetes.io/name=ingress-nginx",
	                                  "-o=json",
	                                  "--kubeconfig",configPath});
		
		if(result.status!=0)
			throw std::runtime_error("kubectl failed: "+result.error);
		
		json.Parse(result.output.c_str());	

		if(!json.HasMember("items") || !json["items"].IsArray())
			throw std::runtime_error("Malformed JSON from kubectl");
		if(json["items"].Size()==0) //if still nothing, the controller is not installed
			return ClusterComponent::NotInstalled;
		else //otherwise we know it's old
			return ClusterComponent::OutOfDate;
	}
	
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


void Client::removeIngressController(const std::string& configPath, const std::string& systemNamespace) const{
	ProgressToken progress(pman_,"Removing ingress controller");
	auto objects=kubernetes::findAll(configPath,"slate-ingress-version",systemNamespace,"get,delete");
	if(objects.empty()){
		throw std::runtime_error("Failed to remove ingress controller: Unable to locate Kubernetes objects to delete");
	}
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
