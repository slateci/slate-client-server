#include <client/Client.h>

#include <iostream>

#include "KubeInterface.h"
#include <Utilities.h>

const static std::string namespacePlaceholder="{{SLATE_NAMESPACE}}";
const static std::string componentVersionPlaceholder="{{COMPONENT_VERSION}}";

const static std::string ingressControllerVersion="v1";
// a templated version of the resources/nginx-ingress.yaml file
const static std::string ingressControllerConfig=
R"(---
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
apiVersion: v1
data:
  allow-snippet-annotations: "true"
kind: ConfigMap
metadata:
  labels:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-controller
  namespace: {{SLATE_NAMESPACE}}
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
apiVersion: v1
kind: ServiceAccount
metadata:
  labels:
    app.kubernetes.io/component: admission-webhook
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-admission
  namespace: {{SLATE_NAMESPACE}}
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: slate-nginx-ingress-clusterrole
  labels:
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
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
      - namespaces
    verbs:
      - list
      - watch
  - apiGroups:
      - coordination.k8s.io
    resources:
      - leases
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
      - networking.k8s.io
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
      - networking.k8s.io
    resources:
      - ingresses/status
    verbs:
      - update
  - apiGroups:
      - networking.k8s.io
    resources:
      - ingressclasses
    verbs:
      - get
      - list
      - watch
  - apiGroups:
      - discovery.k8s.io
    resources:
      - endpointslices
    verbs:
      - list
      - watch
      - get

---
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  name: slate-nginx-ingress-role
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
    slate-ingress-version: {{COMPONENT_VERSION}}
rules:
  - apiGroups:
      - ""
    resources:
      - namespaces
    verbs:
      - get
  - apiGroups:
      - ""
    resources:
      - configmaps
      - pods
      - secrets
      - endpoints
      - services
    verbs:
      - get
      - list
      - watch
  - apiGroups:
      - networking.k8s.io
    resources:
      - ingresses
    verbs:
      - get
      - list
      - watch
  - apiGroups:
      - networking.k8s.io
    resources:
      - ingresses/status
    verbs:
      - update
  - apiGroups:
      - networking.k8s.io
    resources:
      - ingressclasses
    verbs:
      - get
      - list
      - watch
  - apiGroups:
      - ""
    resourceNames:
      - ingress-controller-leader
    resources:
      - configmaps
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
      - coordination.k8s.io
    resourceNames:
      - ingress-controller-leader
    resources:
      - leases
    verbs:
      - get
      - update
  - apiGroups:
      - coordination.k8s.io
    resources:
      - leases
    verbs:
      - create
  - apiGroups:
      - ""
    resources:
      - events
    verbs:
      - create
      - patch
  - apiGroups:
      - discovery.k8s.io
    resources:
      - endpointslices
    verbs:
      - list
      - watch
      - get
---
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  labels:
    app.kubernetes.io/component: admission-webhook
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-admission
  namespace: {{SLATE_NAMESPACE}}
rules:
  - apiGroups:
      - ""
    resources:
      - secrets
    verbs:
      - get
      - create
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  labels:
    app.kubernetes.io/component: admission-webhook
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-admission
rules:
  - apiGroups:
      - admissionregistration.k8s.io
    resources:
      - validatingwebhookconfigurations
    verbs:
      - get
      - update
---
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: slate-nginx-ingress-role-nisa-binding
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
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
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  labels:
    app.kubernetes.io/component: admission-webhook
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
    slate-ingress-version: {{COMPONENT_VERSION}}
  name: ingress-nginx-admission
  namespace: {{SLATE_NAMESPACE}}
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: Role
  name: ingress-nginx-admission
subjects:
  - kind: ServiceAccount
    name: ingress-nginx-admission
    namespace: {{SLATE_NAMESPACE}}
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: slate-nginx-ingress-clusterrole-nisa-binding
  labels:
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
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
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  labels:
    app.kubernetes.io/component: admission-webhook
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
    slate-ingress-version: {{COMPONENT_VERSION}}
  name: ingress-nginx-admission
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: ingress-nginx-admission
subjects:
  - kind: ServiceAccount
    name: ingress-nginx-admission
    namespace: {{SLATE_NAMESPACE}}
---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: nginx-ingress-controller
  namespace: {{SLATE_NAMESPACE}}
  labels:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
    slate-ingress-version: {{COMPONENT_VERSION}}
spec:
  replicas: 1
  selector:
    matchLabels:
      app.kubernetes.io/component: controller
      app.kubernetes.io/instance: ingress-nginx
      app.kubernetes.io/name: ingress-nginx
  template:
    metadata:
      labels:
        app.kubernetes.io/component: controller
        app.kubernetes.io/name: ingress-nginx
        app.kubernetes.io/instance: ingress-nginx
      annotations:
        prometheus.io/port: "10254"
        prometheus.io/scrape: "true"
    spec:
      serviceAccountName: slate-nginx-ingress-serviceaccount
      containers:
        - name: nginx-ingress-controller
          image: registry.k8s.io/ingress-nginx/controller:v1.4.0@sha256:34ee929b111ffc7aa426ffd409af44da48e5a0eea1eb2207994d9e0c0882d143
          args:
            - /nginx-ingress-controller
            - --ingress-class=slate
            - --annotations-prefix=nginx.ingress.kubernetes.io
            - --publish-service=$(POD_NAMESPACE)/ingress-nginx-controller
            - --election-id=ingress-controller-leader
            - --controller-class=k8s.io/ingress-nginx
            - --configmap=$(POD_NAMESPACE)/ingress-nginx-controller
            - --validating-webhook=:8443
            - --validating-webhook-certificate=/usr/local/certificates/cert
            - --validating-webhook-key=/usr/local/certificates/key
          securityContext:
            allowPrivilegeEscalation: true
            capabilities:
              drop:
                - ALL
              add:
                - NET_BIND_SERVICE
            # www-data -> 101
            runAsUser: 101
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
            - name: webhook
              containerPort: 8443
              protocol: TCP
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
          volumeMounts:
            - mountPath: /usr/local/certificates/
              name: webhook-cert
              readOnly: true
      volumes:
        - name: webhook-cert
          secret:
            secretName: ingress-nginx-admission
---
apiVersion: v1
kind: Service
metadata:
  labels:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-controller
  namespace: {{SLATE_NAMESPACE}}
spec:
  externalTrafficPolicy: Local
  ipFamilies:
    - IPv4
  ipFamilyPolicy: SingleStack
  ports:
    - appProtocol: http
      name: http
      port: 80
      protocol: TCP
      targetPort: http
    - appProtocol: https
      name: https
      port: 443
      protocol: TCP
      targetPort: https
  selector:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
  type: LoadBalancer
---
apiVersion: v1
kind: Service
metadata:
  labels:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-controller-admission
  namespace: {{SLATE_NAMESPACE}}
spec:
  ports:
    - appProtocol: https
      name: https-webhook
      port: 443
      targetPort: webhook
  selector:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
  type: ClusterIP
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
apiVersion: batch/v1
kind: Job
metadata:
  labels:
    app.kubernetes.io/component: admission-webhook
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-admission-create
  namespace: {{SLATE_NAMESPACE}}
spec:
  template:
    metadata:
      labels:
        app.kubernetes.io/component: admission-webhook
        app.kubernetes.io/instance: ingress-nginx
        app.kubernetes.io/name: ingress-nginx
        app.kubernetes.io/part-of: ingress-nginx
        app.kubernetes.io/version: 1.4.0
      name: ingress-nginx-admission-create
    spec:
      containers:
        - args:
            - create
            - --host=ingress-nginx-controller-admission,ingress-nginx-controller-admission.$(POD_NAMESPACE).svc
            - --namespace=$(POD_NAMESPACE)
            - --secret-name=ingress-nginx-admission
          env:
            - name: POD_NAMESPACE
              valueFrom:
                fieldRef:
                  fieldPath: metadata.namespace
          image: registry.k8s.io/ingress-nginx/kube-webhook-certgen:v20220916-gd32f8c343@sha256:39c5b2e3310dc4264d638ad28d9d1d96c4cbb2b2dcfb52368fe4e3c63f61e10f
          imagePullPolicy: IfNotPresent
          name: create
          securityContext:
            allowPrivilegeEscalation: false
      nodeSelector:
        kubernetes.io/os: linux
      restartPolicy: OnFailure
      securityContext:
        fsGroup: 2000
        runAsNonRoot: true
        runAsUser: 2000
      serviceAccountName: ingress-nginx-admission
---
apiVersion: batch/v1
kind: Job
metadata:
  labels:
    app.kubernetes.io/component: admission-webhook
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-admission-patch
  namespace: {{SLATE_NAMESPACE}}
spec:
  template:
    metadata:
      labels:
        app.kubernetes.io/component: admission-webhook
        app.kubernetes.io/instance: ingress-nginx
        app.kubernetes.io/name: ingress-nginx
        app.kubernetes.io/part-of: ingress-nginx
        app.kubernetes.io/version: 1.4.0
      name: ingress-nginx-admission-patch
    spec:
      containers:
        - args:
            - patch
            - --webhook-name=ingress-nginx-admission
            - --namespace=$(POD_NAMESPACE)
            - --patch-mutating=false
            - --secret-name=ingress-nginx-admission
            - --patch-failure-policy=Fail
          env:
            - name: POD_NAMESPACE
              valueFrom:
                fieldRef:
                  fieldPath: metadata.namespace
          image: registry.k8s.io/ingress-nginx/kube-webhook-certgen:v20220916-gd32f8c343@sha256:39c5b2e3310dc4264d638ad28d9d1d96c4cbb2b2dcfb52368fe4e3c63f61e10f
          imagePullPolicy: IfNotPresent
          name: patch
          securityContext:
            allowPrivilegeEscalation: false
      nodeSelector:
        kubernetes.io/os: linux
      restartPolicy: OnFailure
      securityContext:
        fsGroup: 2000
        runAsNonRoot: true
        runAsUser: 2000
      serviceAccountName: ingress-nginx-admission
---
apiVersion: networking.k8s.io/v1
kind: IngressClass
metadata:
  labels:
    app.kubernetes.io/component: controller
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: nginx
spec:
  controller: k8s.io/ingress-nginx
---
apiVersion: admissionregistration.k8s.io/v1
kind: ValidatingWebhookConfiguration
metadata:
  labels:
    app.kubernetes.io/component: admission-webhook
    app.kubernetes.io/instance: ingress-nginx
    app.kubernetes.io/name: ingress-nginx
    app.kubernetes.io/part-of: ingress-nginx
    app.kubernetes.io/version: 1.4.0
  name: ingress-nginx-admission
webhooks:
  - admissionReviewVersions:
      - v1
    clientConfig:
      service:
        name: ingress-nginx-controller-admission
        namespace: {{SLATE_NAMESPACE}}
        path: /networking/v1/ingresses
    failurePolicy: Fail
    matchPolicy: Equivalent
    name: validate.nginx.ingress.kubernetes.io
    rules:
      - apiGroups:
          - networking.k8s.io
        apiVersions:
          - v1
        operations:
          - CREATE
          - UPDATE
        resources:
          - ingresses
    sideEffects: None
)";


Client::ClusterComponent::ComponentStatus Client::checkIngressController(const std::string& configPath, const std::string& systemNamespace) const{
	auto result=runCommand("kubectl",{"get","deployments","-n",systemNamespace,
	                                  "-l=slate-ingress-version",
	                                  "-o=json",
	                                  "--kubeconfig",configPath});
	if (result.status != 0) {
		throw std::runtime_error("kubectl failed: " + result.error);
	}
	
	rapidjson::Document json;
	json.Parse(result.output.c_str());

	if (!json.HasMember("items") || !json["items"].IsArray()) {
		throw std::runtime_error("Malformed JSON from kubectl");
	}
	if(json["items"].Size()==0){ //found nothing
		//try looking for a version too old to have the version label
		result=runCommand("kubectl",{"get","deployments","-n",systemNamespace,
	                                  "-l=app.kubernetes.io/name=ingress-nginx",
	                                  "-o=json",
	                                  "--kubeconfig",configPath});

		if (result.status != 0) {
			throw std::runtime_error("kubectl failed: " + result.error);
		}
		
		json.Parse(result.output.c_str());

		if (!json.HasMember("items") || !json["items"].IsArray()) {
			throw std::runtime_error("Malformed JSON from kubectl");
		}
		if (json["items"].Size() == 0) { //if still nothing, the controller is not installed
			return ClusterComponent::NotInstalled;
		} else {
			//otherwise we know it's old
			return ClusterComponent::OutOfDate;
		}
	}
	
	//TODO: find and compare version
	if (!json["items"][0].IsObject() || !json["items"][0].HasMember("metadata") ||
	    !json["items"][0]["metadata"].IsObject()) {
		throw std::runtime_error("Malformed JSON from kubectl");
	}
	
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
	while ((pos = ingressControllerConfig.find(namespacePlaceholder)) != std::string::npos) {
		ingressControllerConfig.replace(pos, namespacePlaceholder.size(), systemNamespace);
	}
	while ((pos = ingressControllerConfig.find(componentVersionPlaceholder)) != std::string::npos) {
		ingressControllerConfig.replace(pos, componentVersionPlaceholder.size(), ingressControllerVersion);
	}
	auto result=runCommandWithInput("kubectl",ingressControllerConfig,{"apply","--kubeconfig",configPath,"-f","-"});
	if (result.status) {
		throw std::runtime_error("Failed to install ingress controller: " + result.error);
	}
}


void Client::removeIngressController(const std::string& configPath, const std::string& systemNamespace) const{
	ProgressToken progress(pman_,"Removing ingress controller");
	auto objects=kubernetes::findAll(configPath,"slate-ingress-version",systemNamespace,"get,delete");
	if(objects.empty()){
		throw std::runtime_error("Failed to remove ingress controller: Unable to locate Kubernetes objects to delete");
	}
	std::vector<std::string> deleteArgs={"delete","--kubeconfig="+configPath,"-n="+systemNamespace,"--ignore-not-found"};
	deleteArgs.reserve(deleteArgs.size()+objects.size());
	for (const auto &object: objects) {
		deleteArgs.push_back(object.first + "/" + object.second);
	}
	auto result=runCommand("kubectl",deleteArgs);
	if (result.status != 0) {
		throw std::runtime_error("Failed to remove ingress controller: " + result.error);
	}
}

void Client::upgradeIngressController(const std::string& configPath, const std::string& systemNamespace) const{
	try{
		auto status=checkIngressController(configPath,systemNamespace);
		if (status == ClusterComponent::OutOfDate) {
			removeIngressController(configPath, systemNamespace);
		}
		if (status != ClusterComponent::UpToDate) {
			installIngressController(configPath, systemNamespace);
		} else {
			std::cout << "Nothing to do" << std::endl;
		}
	}
	catch(std::runtime_error& err){
		throw std::runtime_error("Failed to upgrade ingress controller: "+std::string(err.what()));
	}
}
