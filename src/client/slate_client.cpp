#include <memory>

#include "CLI11.hpp"

#include "client/Client.h"
#include "client/SecretLoading.h"
#include "client/Completion.h"
#include "Process.h"

void registerVersionCommand(CLI::App& parent, Client& client){
	auto version = parent.add_subcommand("version", "Print version information");
	version->callback([&client](){ client.printVersion(); });
	
	auto options = std::make_shared<upgradeOptions>();
	auto upgrade = version->add_subcommand("upgrade", "Upgrade to the latest released version");
	upgrade->add_flag("-y,--assumeyes", options->assumeYes, "Assume yes, or the default answer, to any question which would be asked");
    upgrade->callback([&client,options](){ client.upgrade(*options); });
}

void registerCompletionCommand(CLI::App& parent, Client& client){
	auto shell = std::make_shared<std::string>();
	auto completion = parent.add_subcommand("completion", "Print a shell completion script");
	completion->add_option("shell", *shell, "The shell for which to produce a completion script")->envname("SHELL");
	completion->callback([shell,&parent](){ getCompletionScript(parent, *shell); });
}

void registerGroupList(CLI::App& parent, Client& client){
    auto groupListOpt = std::make_shared<GroupListOptions>();
    auto list = parent.add_subcommand("list", "List groups");
    list->callback([&client, groupListOpt](){ client.listGroups(*groupListOpt); });
    list->add_flag("--user", groupListOpt->user, "Show only groups to which you belong");
}

void registerGroupInfo(CLI::App& parent, Client& client){
    auto groupInfoOpt = std::make_shared<GroupInfoOptions>();
    auto info = parent.add_subcommand("info", "Get information about a group");
    info->callback([&client, groupInfoOpt](){ client.getGroupInfo(*groupInfoOpt); });
    info->add_option("group-name", groupInfoOpt->groupName, "The name or ID of the group to look up")->required();
}

void registerGroupListAllowed(CLI::App& parent, Client& client){
    auto opt = std::make_shared<GroupListAllowedOptions>();
    auto info = parent.add_subcommand("list-allowed-clusters", "List all clusters that a group can access");
    info->callback([&client, opt](){ client.listClustersAccessibleToGroup(*opt); });
    info->add_option("group-name", opt->groupName, "The name or ID of the group to look up")->required();
}

void registerGroupCreate(CLI::App& parent, Client& client){
    auto groupCreateOpt = std::make_shared<GroupCreateOptions>();
    auto create = parent.add_subcommand("create", "Create a new group");
    create->add_option("group-name", groupCreateOpt->groupName, "Name of the group to create")->required();    
    create->add_option("--field", groupCreateOpt->scienceField, "The field of science on which the group in focused. "
      "See http://slateci.io/docs/science-fields for a list of accepted values")->required();
    create->callback([&client,groupCreateOpt](){ client.createGroup(*groupCreateOpt); });
}

void registerGroupUpdate(CLI::App& parent, Client& client){
    auto groupUpdateOpt = std::make_shared<GroupUpdateOptions>();
    auto update = parent.add_subcommand("update", "UPdate one or more of a group's properties");
    update->add_option("group-name", groupUpdateOpt->groupName, "Name of the group to alter")->required();
    update->add_option("--email", groupUpdateOpt->email, "The contact email address for the group");
    update->add_option("--phone", groupUpdateOpt->phone, "The contact phone number for the group");
    update->add_option("--field", groupUpdateOpt->scienceField, "The field of science on which the group in focused");
    update->add_option("--desc", groupUpdateOpt->description, "The description of the group");
    update->callback([&client,groupUpdateOpt](){ client.updateGroup(*groupUpdateOpt); });
}

void registerGroupDelete(CLI::App& parent, Client& client){
    auto groupDeleteOpt = std::make_shared<GroupDeleteOptions>();
    auto del = parent.add_subcommand("delete", "Destroy a group");
    del->add_option("group-name", groupDeleteOpt->groupName, "Name of the group to delete")->required();
	del->add_flag("-y,--assume-yes", groupDeleteOpt->assumeYes, "Assume yes to any deletion confirmation, suppressing it");
    del->callback([&client,groupDeleteOpt](){ client.deleteGroup(*groupDeleteOpt); });
}

void registerGroupCommands(CLI::App& parent, Client& client){
	auto group = parent.add_subcommand("group", "Manage SLATE groups");
	group->require_subcommand();
	registerGroupList(*group, client);
	registerGroupInfo(*group, client);
	registerGroupListAllowed(*group, client);
	registerGroupCreate(*group, client);
	registerGroupUpdate(*group, client);
	registerGroupDelete(*group, client);
}

void registerClusterList(CLI::App& parent, Client& client){
    auto clusterListOpt = std::make_shared<ClusterListOptions>();
    auto list = parent.add_subcommand("list", "List clusters");
    list->add_option("--group", clusterListOpt->group, "Show only clusters this group is allowed on");
    list->callback([&client, clusterListOpt](){ client.listClusters(*clusterListOpt); });
}

void registerClusterInfo(CLI::App& parent, Client& client){
    auto clusterInfoOpt = std::make_shared<ClusterInfoOptions>();
    auto info = parent.add_subcommand("info", "Get information about a cluster");
    info->add_option("cluster-name", clusterInfoOpt->clusterName, "The name or ID of the cluster to look up")->required();
	info->add_flag("--all-nodes", clusterInfoOpt->all_nodes, "Include the IP of every node in this query");
	info->callback([&client, clusterInfoOpt](){ client.getClusterInfo(*clusterInfoOpt); });
}

void registerClusterCreate(CLI::App& parent, Client& client){
    auto clusterCreateOpt = std::make_shared<ClusterCreateOptions>();
    auto create = parent.add_subcommand("create", "Register a cluster with SLATE");
    create->add_option("cluster-name", clusterCreateOpt->clusterName, "Name of the cluster to create")->required();
	create->add_option("--group", clusterCreateOpt->groupName, "Name of the Group which will administer the cluster")->required();
	create->add_option("--org", clusterCreateOpt->orgName, "Name of the organization which owns the cluster hardware")->required();
	create->add_option("--kubeconfig", clusterCreateOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					   "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set.");
	create->add_flag("-y,--assume-yes", clusterCreateOpt->assumeYes, "Assume yes, or the default answer, to any question which would be asked");
	create->add_flag("--assume-load-balancer", clusterCreateOpt->assumeLoadBalancer, "Assume that the cluster has a LoadBalancer even if it is not detectable");
	create->add_flag("--no-ingress", clusterCreateOpt->noIngress, "Do not set up an ingress controller")->group("");
    create->callback([&client,clusterCreateOpt](){ client.createCluster(*clusterCreateOpt); });
}

void registerClusterUpdate(CLI::App& parent, Client& client){
    auto clusterUpdateOpt = std::make_shared<ClusterUpdateOptions>();
    auto update = parent.add_subcommand("update", "Update a cluster's information");
    update->add_option("cluster-name", clusterUpdateOpt->clusterName, "Name of the cluster to update")->required();
	update->add_option("--org", clusterUpdateOpt->orgName, "Name of the organization which owns the cluster hardware");
	update->add_flag("-r,--reconfigure", clusterUpdateOpt->reconfigure, "Update the kubeconfig used to contact the cluster");
	update->add_option("--kubeconfig", clusterUpdateOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					   "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set. Implies --reconfigure.");
	update->add_flag("-y,--assume-yes", clusterUpdateOpt->assumeYes, "Assume yes, or the default answer, to any question which would be asked");
	
	update->add_option("--location", [=](const std::vector<std::string>& args)->bool{
	                   	for(const auto& arg : args){
	                   		std::istringstream ss(arg);
	                   		GeoLocation loc;
	                   		ss >> loc;
	                   		if(ss.fail())
	                   			throw std::runtime_error("Unable to parse '"+arg+"' as a geographic location");
	                   		clusterUpdateOpt->locations.push_back(loc);
	                   	}
	                   	return true;
	                   }, "Geographic location (in the form lat,lon)")
	                  ->type_size(-1)->expected(-1);
	
    update->callback([&client,clusterUpdateOpt](){ client.updateCluster(*clusterUpdateOpt); });
}

void registerClusterDelete(CLI::App& parent, Client& client){
    auto clusterDeleteOpt = std::make_shared<ClusterDeleteOptions>();
    auto del = parent.add_subcommand("delete", "Remove a cluster from SLATE");
    del->add_option("cluster-name", clusterDeleteOpt->clusterName, "Name of the cluster to delete")->required();
	del->add_flag("-y,--assume-yes", clusterDeleteOpt->assumeYes, "Assume yes to any deletion confirmation, suppressing it");
	del->add_flag("-f,--force", clusterDeleteOpt->force, "Force deletion even if helm cannot "
	                 "delete instances from the kubernetes cluster. Use with caution, "
	                 "as this can potentially leave running, but undeletable deployments.");
    del->callback([&client,clusterDeleteOpt](){ client.deleteCluster(*clusterDeleteOpt); });
}

void registerClusterComponentList(CLI::App& parent, Client& client){
	auto list = parent.add_subcommand("list", "List the available cluster components");
	list->callback([&client](){ client.listClusterComponents(); });
}

void registerClusterComponentInfo(CLI::App& parent, Client& client){
	auto componentInfoOpt = std::make_shared<ClusterComponentListOptions>();
	auto info = parent.add_subcommand("info", "Get information about installed cluster components");
	info->add_option("--kubeconfig", componentInfoOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					 "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set.");
	info->add_option("-n,--system-namespace",componentInfoOpt->systemNamespace, 
	                 "The SLATE system namespace with respect to which operations should be performed");
	info->add_flag("-v,--verbose",componentInfoOpt->verbose, 
	               "Mention components which are not installed.");
	info->callback([&client,componentInfoOpt](){ client.listInstalledClusterComponents(*componentInfoOpt); });
}

/*void registerClusterCheckComponent(CLI::App& parent, Client& client){
	auto componentCheckOpt = std::make_shared<ClusterComponentOptions>();
	auto check = parent.add_subcommand("check-component", "Check the install status of a component");
	check->add_option("component-name", componentCheckOpt->componentName, "Name of the component to check")->required();
	check->add_option("--kubeconfig", componentCheckOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					   "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set.");
	check->add_option("-n,--system-namespace",componentCheckOpt->systemNamespace, 
	                  "The SLATE system namespace with respect to which operations should be performed");
	check->callback([&client,componentCheckOpt](){ client.checkClusterComponent(*componentCheckOpt); });
}*/

void registerClusterAddComponent(CLI::App& parent, Client& client){
	auto componentAddOpt = std::make_shared<ClusterComponentOptions>();
	auto add = parent.add_subcommand("add", "Install a component");
	add->add_option("component-name", componentAddOpt->componentName, "Name of the component to install")->required();	
	add->add_option("--kubeconfig", componentAddOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					   "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set.");
	add->add_option("-n,--system-namespace",componentAddOpt->systemNamespace, 
	                "The SLATE system namespace with respect to which operations should be performed");
	add->callback([&client,componentAddOpt](){ client.addClusterComponent(*componentAddOpt); });
}

void registerClusterRemoveComponent(CLI::App& parent, Client& client){
	auto componentDelOpt = std::make_shared<ClusterComponentOptions>();
	auto rem = parent.add_subcommand("remove", "Uninstall a component");
	rem->add_option("component-name", componentDelOpt->componentName, "Name of the component to remove")->required();	
	rem->add_option("--kubeconfig", componentDelOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					   "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set.");
	rem->add_option("-n,--system-namespace",componentDelOpt->systemNamespace, 
	                "The SLATE system namespace with respect to which operations should be performed");
	rem->callback([&client,componentDelOpt](){ client.removeClusterComponent(*componentDelOpt); });
}

void registerClusterUpgradeComponent(CLI::App& parent, Client& client){
	auto componentUpOpt = std::make_shared<ClusterComponentOptions>();
	auto upgrade = parent.add_subcommand("upgrade", "Upgrade a component");
	upgrade->add_option("component-name", componentUpOpt->componentName, "Name of the component to remove")->required();	
	upgrade->add_option("--kubeconfig", componentUpOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					   "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set.");
	upgrade->add_option("-n,--system-namespace",componentUpOpt->systemNamespace, 
	                    "The SLATE system namespace with respect to which operations should be performed");
	upgrade->callback([&client,componentUpOpt](){ client.upgradeClusterComponent(*componentUpOpt); });
}

void registerClusterComponent(CLI::App& parent, Client& client){
	auto comp = parent.add_subcommand("component", "Manage components which are installed directly to kubernetes by a cluster admin");
	comp->require_subcommand();
	registerClusterComponentList(*comp, client);
	registerClusterComponentInfo(*comp, client);
	registerClusterAddComponent(*comp, client);
	registerClusterRemoveComponent(*comp, client);
	registerClusterUpgradeComponent(*comp, client);
}

void registerClusterListAllowed(CLI::App& parent, Client& client){
	auto accessOpt = std::make_shared<ClusterAccessListOptions>();
	auto list = parent.add_subcommand("list-allowed-groups", "List groups allowed access to a cluster");
	list->add_option("cluster-name", accessOpt->clusterName, "Name of the cluster")->required();
	list->callback([&client,accessOpt](){ client.listGroupWithAccessToCluster(*accessOpt); });
}

void registerClusterAllowGroup(CLI::App& parent, Client& client){
	auto accessOpt = std::make_shared<GroupClusterAccessOptions>();
	auto allow = parent.add_subcommand("allow-group", "Grant a group access to a cluster");
	allow->add_option("cluster-name", accessOpt->clusterName, "Name of the cluster to give access to")->required();
	allow->add_option("group-name", accessOpt->groupName, "Name of the group to give access")->required();
	allow->callback([&client,accessOpt](){ client.grantGroupClusterAccess(*accessOpt); });
}

void registerClusterDenyGroup(CLI::App& parent, Client& client){
	auto accessOpt = std::make_shared<GroupClusterAccessOptions>();
	auto deny = parent.add_subcommand("deny-group", "Revoke a group's access to a cluster");
	deny->add_option("cluster-name", accessOpt->clusterName, "Name of the cluster to remove access to")->required();
	deny->add_option("group-name", accessOpt->groupName, "Name of the group whose access to remove")->required();
	deny->callback([&client,accessOpt](){ client.revokeGroupClusterAccess(*accessOpt); });
}

void registerListAllowedApplications(CLI::App& parent, Client& client){
	auto listOpt = std::make_shared<GroupClusterAppUseListOptions>();
	auto list = parent.add_subcommand("list-group-allowed-apps", "List applications a group is allowed to use on a cluster");
	list->add_option("cluster-name", listOpt->clusterName, "Name of the cluster")->required();
	list->add_option("group-name", listOpt->groupName, "Name of the group")->required();
	list->callback([&client,listOpt](){ client.listAllowedApplications(*listOpt); });
}

void registerAllowGroupUseOfApplication(CLI::App& parent, Client& client){
	auto useOpt = std::make_shared<GroupClusterAppUseOptions>();
	auto allow = parent.add_subcommand("allow-group-app", "Grant a group permission to use an application on a cluster");
	allow->add_option("cluster-name", useOpt->clusterName, "Name of the cluster")->required();
	allow->add_option("group-name", useOpt->groupName, "Name of the group")->required();
	allow->add_option("app-name", useOpt->appName, "Name of the application")->required();
	allow->callback([&client,useOpt](){ client.allowGroupUseOfApplication(*useOpt); });
}

void registerDenyGroupUseOfApplication(CLI::App& parent, Client& client){
	auto useOpt = std::make_shared<GroupClusterAppUseOptions>();
	auto deny = parent.add_subcommand("deny-group-app", "Remove a group's permission to use an application on a cluster");
	deny->add_option("cluster-name", useOpt->clusterName, "Name of the cluster")->required();
	deny->add_option("group-name", useOpt->groupName, "Name of the group")->required();
	deny->add_option("app-name", useOpt->appName, "Name of the application")->required();
	deny->callback([&client,useOpt](){ client.denyGroupUseOfApplication(*useOpt); });
}

void registerClusterPing(CLI::App& parent, Client& client){
	auto opt = std::make_shared<ClusterPingOptions>();
	auto ping = parent.add_subcommand("ping", "Check whether the platform can connect to a cluster");
	ping->add_option("cluster-name", opt->clusterName, "Name of the cluster")->required();
	ping->callback([&client,opt](){ client.pingCluster(*opt); });
}

void registerClusterCommands(CLI::App& parent, Client& client){
	auto cluster = parent.add_subcommand("cluster", "Manage SLATE clusters");
	cluster->require_subcommand();
	registerClusterList(*cluster, client);
	registerClusterInfo(*cluster, client);
	registerClusterCreate(*cluster, client);
	registerClusterUpdate(*cluster, client);
	registerClusterDelete(*cluster, client);
	registerClusterListAllowed(*cluster, client);
	registerClusterAllowGroup(*cluster, client);
	registerClusterDenyGroup(*cluster, client);
	registerListAllowedApplications(*cluster, client);
	registerAllowGroupUseOfApplication(*cluster, client);
	registerDenyGroupUseOfApplication(*cluster, client);
	registerClusterComponent(*cluster, client);
	registerClusterPing(*cluster, client);
}

void registerApplicationList(CLI::App& parent, Client& client){
	auto appOpt = std::make_shared<ApplicationOptions>();
    auto list = parent.add_subcommand("list", "List available applications");
	list->add_flag("--dev", appOpt->devRepo, "Show applications from the development catalog");
	list->add_flag("--test", appOpt->testRepo, "Show applications from the test catalog")->group("");
    list->callback([&client,appOpt](){ client.listApplications(*appOpt); });
}

void registerApplicationGetConf(CLI::App& parent, Client& client){
	auto appOpt = std::make_shared<ApplicationConfOptions>();
    auto conf = parent.add_subcommand("get-conf", "Get the configuration template for an application");
	conf->add_option("app-name", appOpt->appName, "Name of the application to fetch")->required();
	conf->add_option("-o,--output", appOpt->outputFile, "File to which to write the configuration");
	conf->add_flag("--dev", appOpt->devRepo, "Fetch from the development catalog");
	conf->add_flag("--test", appOpt->testRepo, "Fetch from the test catalog")->group("");
    conf->callback([&client,appOpt](){ client.getApplicationConf(*appOpt); });
}

void registerApplicationGetDocs(CLI::App& parent, Client& client){
	auto appOpt = std::make_shared<ApplicationConfOptions>();
    auto conf = parent.add_subcommand("info", "Get an application's documentation");
	conf->add_option("app-name", appOpt->appName, "Name of the application to fetch")->required();
	conf->add_option("-o,--output", appOpt->outputFile, "File to which to write the documentation");
	conf->add_flag("--dev", appOpt->devRepo, "Fetch from the development catalog");
	conf->add_flag("--test", appOpt->testRepo, "Fetch from the test catalog")->group("");
    conf->callback([&client,appOpt](){ client.getApplicationDocs(*appOpt); });
}

void registerApplicationInstall(CLI::App& parent, Client& client){
	auto appOpt = std::make_shared<ApplicationInstallOptions>();
    auto install = parent.add_subcommand("install", "Install an instance of an application");
	install->add_option("app-name", appOpt->appName, "Name of the application to install")->required();
	install->add_option("--group", appOpt->group, "Name of the group which will own the instance")->required();
	install->add_option("--cluster", appOpt->cluster, "Name of the cluster on which the instance will run")->required();
	install->add_option("--conf", appOpt->configPath, "File containing configuration for the instance");
	install->add_flag("--dev", appOpt->devRepo, "Install from the development catalog");
	install->add_flag("--test", appOpt->testRepo, "Install from the test catalog")->group("");
	install->add_flag("--local", appOpt->fromLocalChart, "Install a local chart directly");
    install->callback([&client,appOpt](){ client.installApplication(*appOpt); });
}

void registerApplicationCommands(CLI::App& parent, Client& client){
	auto app = parent.add_subcommand("app", "View and install SLATE applications");
	app->require_subcommand();
	registerApplicationList(*app, client);
	registerApplicationGetConf(*app, client);
	registerApplicationGetDocs(*app, client);
	registerApplicationInstall(*app, client);
}

void registerInstanceList(CLI::App& parent, Client& client){
	auto instOpt = std::make_shared<InstanceListOptions>();
    auto list = parent.add_subcommand("list", "List deployed application instances");
	list->add_option("--group", instOpt->group, "Show only instances belonging to this group");
	list->add_option("--cluster", instOpt->cluster, "Show only instances running on this cluster");
    list->callback([&client,instOpt](){ client.listInstances(*instOpt); });
}

void registerInstanceInfo(CLI::App& parent, Client& client){
	auto instOpt = std::make_shared<InstanceOptions>();
    auto info = parent.add_subcommand("info", "Fetch information about a deployed instance");
	info->add_option("instance", instOpt->instanceID, "The ID of the instance")->required();
    info->callback([&client,instOpt](){ client.getInstanceInfo(*instOpt); });
}

void registerInstanceRestart(CLI::App& parent, Client& client){
	auto restOpt = std::make_shared<InstanceOptions>();
    auto restart = parent.add_subcommand("restart", "Stop and restart a deployed instance");
	restart->add_option("instance", restOpt->instanceID, "The ID of the instance")->required();
    restart->callback([&client,restOpt](){ client.restartInstance(*restOpt); });
}

void registerInstanceDelete(CLI::App& parent, Client& client){
	auto delOpt = std::make_shared<InstanceDeleteOptions>();
    auto del = parent.add_subcommand("delete", "Destroy an application instance");
	del->add_option("instance", delOpt->instanceID, "The ID of the instance")->required();
	del->add_flag("-f,--force", delOpt->force, "Force deletion even if helm cannot "
	                 "delete the instance from the kubernetes cluster. Use with caution, "
	                 "as this can potentially leave a running, but undeletable deployment.");
	del->add_flag("-y,--assume-yes", delOpt->assumeYes, "Assume yes to any deletion confirmation, suppressing it");
    del->callback([&client,delOpt](){ client.deleteInstance(*delOpt); });
}

void registerInstanceFetchLogs(CLI::App& parent, Client& client){
	auto instOpt = std::make_shared<InstanceLogOptions>();
    auto info = parent.add_subcommand("logs", "Get logs from an application instance");
	info->add_option("instance", instOpt->instanceID, "The ID of the instance")->required();
	info->add_option("--max-lines", instOpt->maxLines, "Maximum number of most recent lines to fetch, 0 to get full logs");
	info->add_option("--container", instOpt->container, "Name of specific container for which to fetch logs");
	info->add_flag("--previous", instOpt->previousLogs, "Name of specific container for which to fetch logs");
    info->callback([&client,instOpt](){ client.fetchInstanceLogs(*instOpt); });
}

void registerInstanceScale(CLI::App& parent, Client& client){
	auto instOpt = std::make_shared<InstanceScaleOptions>();
    auto info = parent.add_subcommand("scale", "View or set the number of replicas of an instance");
	info->add_option("instance", instOpt->instanceID, "The ID of the instance")->required();
	info->add_option("--replicas", instOpt->instanceReplicas, "Integer number of replicas to scale an instance up or down. If unspecified, return the current number of replicas.");
	info->add_option("--deployment", instOpt->deployment, "The deployment to operate on. Optional for viewing, but required for scaling a deployment when the application contains multiple.");
    info->callback([&client,instOpt](){ client.scaleInstance(*instOpt); });
}

void registerInstanceCommands(CLI::App& parent, Client& client){
	auto inst = parent.add_subcommand("instance", "Manage SLATE application instances");
	inst->require_subcommand();
	registerInstanceList(*inst, client);
	registerInstanceInfo(*inst, client);
	registerInstanceRestart(*inst, client);
	registerInstanceDelete(*inst, client);
	registerInstanceFetchLogs(*inst, client);
	registerInstanceScale(*inst, client);
}

void registerSecretList(CLI::App& parent, Client& client){
	auto secrOpt = std::make_shared<SecretListOptions>();
	auto list = parent.add_subcommand("list", "List secrets");
	list->add_option("--group", secrOpt->group, "Show only secrets belonging to this group")->required();
	list->add_option("--cluster", secrOpt->cluster, "Show only secrets on this cluster");
	list->callback([&client,secrOpt](){ client.listSecrets(*secrOpt); });
}

void registerSecretInfo(CLI::App& parent, Client& client){
	auto secrOpt = std::make_shared<SecretOptions>();
	auto info = parent.add_subcommand("info", "Fetch information about a secret");
	info->add_option("secret", secrOpt->secretID, "The ID of the secret")->required();
	info->callback([&client,secrOpt](){ client.getSecretInfo(*secrOpt); });
}

void registerSecretCreate(CLI::App& parent, Client& client){
	auto secrCreateOpt = std::make_shared<SecretCreateOptions>();
	auto create = parent.add_subcommand("create", "Create a new secret");
	create->add_option("secret-name", secrCreateOpt->name, "Name of the secret to create")->required();
	create->add_option("--group", secrCreateOpt->group, "Group for which to create secret")->required();
	create->add_option("--cluster", secrCreateOpt->cluster, "Cluster to create secret on")->required();

	//input for "key and literal value to insert in secret, ie mykey=somevalue
	create->add_option("--from-literal", [=](const std::vector<std::string>& args)->bool{
	                   	for(const auto& arg : args)
	                   		secrCreateOpt->data.push_back(arg);
	                   	return true;
	                   }, "Key and literal value to add to secret (in the form key=value)")
	                  ->type_size(-1)->expected(-1);
	//input for a key which is a file name with the value being implicitly the contents of that file
	create->add_option("--from-file", [=](std::vector<std::string> args)->bool{
	                   	for(const auto& arg : args)
	                   		parseFromFileSecretEntry(arg,secrCreateOpt->data);
	                   	return true;
	                   }, 
					   "Filename to use as key with file contents used as the "
					   "value. The path at which the file should be recreated "
					   "may be optionally specified after an equals sign")
	                  ->type_size(-1)->expected(-1);
	//input for a set on keys and values stored in a Docker-style environment file
	create->add_option("--from-env-file", 
	                   [=](std::vector<std::string> args)->bool{
	                   	for(const auto& arg : args)
	                   		parseFromEnvFileSecretEntry(arg,secrCreateOpt->data);
	                   	return true;
	                   }, "Path to a file from which to read lines of key=value "
	                   "pairs to add to the secret")
	                  ->type_size(-1)->expected(-1);
	
	create->callback([&client,secrCreateOpt](){ client.createSecret(*secrCreateOpt); });
}

void registerSecretCopy(CLI::App& parent, Client& client){
	auto secrCopyOpt = std::make_shared<SecretCopyOptions>();
	auto copy = parent.add_subcommand("copy", "Copy a secret to another cluster");
	copy->add_option("source-id", secrCopyOpt->sourceID, "ID of the source secret")->required();
	copy->add_option("secret-name", secrCopyOpt->name, "Name of the secret to create")->required();
	copy->add_option("--group", secrCopyOpt->group, "Group for which to create secret")->required();
	copy->add_option("--cluster", secrCopyOpt->cluster, "Cluster to create secret on")->required();
	copy->callback([&client,secrCopyOpt](){ client.copySecret(*secrCopyOpt); });
}

void registerSecretDelete(CLI::App& parent, Client& client){
	auto secrDeleteOpt = std::make_shared<SecretDeleteOptions>();
	auto del = parent.add_subcommand("delete", "Remove a secret from SLATE");
	del->add_option("secret", secrDeleteOpt->secretID, "ID of the secret to delete")->required();
	del->add_flag("-f,--force", secrDeleteOpt->force, "Force deletion even if the secret "
	                 "cannot be deleted from the kubernetes cluster. Use with caution, "
	                 "as this can potentially leave an existing, but invisible secret.");
	del->add_flag("-y,--assume-yes", secrDeleteOpt->assumeYes, "Assume yes to any deletion confirmation, suppressing it");
	del->callback([&client,secrDeleteOpt](){ client.deleteSecret(*secrDeleteOpt); });
}

void registerSecretCommands(CLI::App& parent, Client& client){
	auto secr = parent.add_subcommand("secret", "Manage SLATE secrets");
	secr->require_subcommand();
	registerSecretList(*secr, client);
	registerSecretInfo(*secr, client);
	registerSecretCreate(*secr, client);
	registerSecretCopy(*secr, client);
	registerSecretDelete(*secr, client);
}

void registerWhoAmI(CLI::App& parent, Client& client) {
	auto who = parent.add_subcommand("whoami", "Fetch current user credentials");
	who->callback([&client](){ client.fetchCurrentProfile(); });
}

void registerUserList(CLI::App& parent, Client& client) {
	auto lOpt = std::make_shared<UserOptions>();
	auto usrlist = parent.add_subcommand("list", "List SLATE users on cluster");
	usrlist->add_option("--group", lOpt->group, "Group filter (name or ID)");
	usrlist->callback([&client, lOpt](){ client.listUsers(*lOpt); });
}

void registerUserInfo(CLI::App& parent, Client& client) {
	auto infoOpt = std::make_shared<UserOptions>();
	auto info = parent.add_subcommand("info", "Get info about a SLATE user");
	info->add_option("user", infoOpt->id, "ID of the user being queried")->required();
	info->callback([&client, infoOpt](){ client.getUserInfo(*infoOpt); });
}

void registerUserGroupList(CLI::App& parent, Client& client) {
	auto gplOpt = std::make_shared<UserOptions>();
	auto grouplist = parent.add_subcommand("groups", "List groups of SLATE user");
	grouplist->add_option("user", gplOpt->id, "ID of the user being queried")->required();
	grouplist->callback([&client, gplOpt](){ client.listUserGroups(*gplOpt); });
}

// Not used currently
// void registerUserAdd(CLI::App& parent, Client& client) {
// 	auto usrCreateOpt = std::make_shared<CreateUserOptions>();
// 	auto usrcreate = parent.add_subcommand("create", "Create a new user");
// 	usrCreate->add_option("--globus", usrCreateOpt->globID, "Globus ID of the user")->required();
// 	usrCreate->add_option("--name", usrCreateOpt->name, "Name of the user")->required();
// 	usrCreate->add_option("--email", usrCreateOpt->email, "Email of the user")->required();
// 	usrCreate->add_option("--phone", usrCreateOpt->phone, "Phone of the user")->required();
// 	usrCreate->add_option("--institution", usrCreateOpt->institution, "Institution of the user")->required();
// 	usrCreate->add_flag("-a,--admin", usrCreateOpt->admin, "This user is an administrator");
// 	usrCreate->callback();
// }

void registerUserDelete(CLI::App& parent, Client& client) {
	auto usrDeleteOpt = std::make_shared<UserOptions>();
	auto del = parent.add_subcommand("delete", "Delete slate user");
	del->add_option("user", usrDeleteOpt->id, "ID of the user being deleted")->required();
	del->callback([&client, usrDeleteOpt](){ client.removeUser(*usrDeleteOpt); });
}

void registerUserUpdate(CLI::App& parent, Client& client) {
	auto updateOpt = std::make_shared<UpdateUserOptions>();
	auto upd = parent.add_subcommand("update", "Update user profile");
	upd->add_option("user", updateOpt->id, "ID of the user (if not included this will update your own profile)");
	upd->add_option("--name", updateOpt->name, "Name of the user");
	upd->add_option("--email", updateOpt->email, "Email of the user");
	upd->add_option("--phone", updateOpt->phone, "Phone of the user");
	upd->add_option("--institution", updateOpt->institution, "Institution of the user");
	upd->callback([&client, updateOpt](){ client.updateUser(*updateOpt); });
}

void registerAddUserToGroup(CLI::App& parent, Client& client) {
	auto membrOpt = std::make_shared<UserOptions>();
	auto groupOp = parent.add_subcommand("add-to-group", "Add user to group");
	groupOp->add_option("user", membrOpt->id, "ID of the user")->required();
	groupOp->add_option("group", membrOpt->group, "ID or name of the group to be added to")->required();
	groupOp->callback([&client, membrOpt](){ client.addUserToGroup(*membrOpt); });
}

void registerRemoveUserFromGroup(CLI::App& parent, Client& client) {
	auto membrOpt = std::make_shared<UserOptions>();
	auto groupOp = parent.add_subcommand("remove-from-group", "Remove user from group");
	groupOp->add_option("user", membrOpt->id, "ID of the user")->required();
	groupOp->add_option("group", membrOpt->group, "ID or name of the group to be removed from")->required();
	groupOp->callback([&client, membrOpt](){ client.removeUserFromGroup(*membrOpt); });
}

void registerUserUpdateToken(CLI::App& parent, Client& client) {
	auto membrOpt = std::make_shared<UserOptions>();
	auto tokOp = parent.add_subcommand("replace-token", "Generate a new token for a user");
	tokOp->add_option("user", membrOpt->id, "ID of the user (if not included this will update your own profile)");
	tokOp->callback([&client, membrOpt](){ client.updateUserToken(*membrOpt); });
}

void registerUserCommands(CLI::App& parent, Client& client) {
	auto usrcmd = parent.add_subcommand("user", "Manage SLATE users");
	usrcmd->require_subcommand();
	registerUserList(*usrcmd, client);
	registerUserInfo(*usrcmd, client);
	registerUserGroupList(*usrcmd, client);
	//registerUserAdd(*usrcmd, client);
	registerUserDelete(*usrcmd, client);
	registerUserUpdate(*usrcmd, client);
	registerAddUserToGroup(*usrcmd, client);
	registerRemoveUserFromGroup(*usrcmd, client);
	registerUserUpdateToken(*usrcmd, client);
}

void registerCommonOptions(CLI::App& parent, Client& client){
	parent.add_option("--orderBy", client.orderBy, "the name of a column in the JSON output"
			"by which to order the table printed to stdout.");
	parent.add_flag_function("--no-format", 
	                         [&](std::size_t){ client.setUseANSICodes(false); }, 
	                         "Do not use ANSI formatting escape sequences in output");
	parent.add_option("--width",client.outputWidth,
	                  "The maximum width to use when printing tabular output");
	parent.add_option("--api-endpoint",client.apiEndpoint,
	                  "The endpoint at which to contact the SLATE API server")
	                 ->envname("SLATE_API_ENDPOINT")
	                 ->type_name("URL");
	parent.add_option("--api-endpoint-file",client.endpointPath,
	                  "The path to a file containing the endpoint at which to "
	                  "contact the SLATE API server. The contents of this file "
	                  "are overridden by --api-endpoint if that option is "
	                  "specified. Ignored if the specified file does not exist.")
	                 ->envname("SLATE_API_ENDPOINT_PATH")
	                 ->type_name("PATH");
	parent.add_option("--credential-file",client.credentialPath,
	                  "The path to a file containing the credentials to be "
	                  "presented to the SLATE API server")
	                 ->envname("SLATE_CRED_PATH")
	                 ->type_name("PATH");
	parent.add_option("--output",client.outputFormat,
			  "The format in which to print output (can be specified as no-headers, json, jsonpointer, jsonpointer-file, custom-columns, or custom-columns-file)");
#ifdef USE_CURLOPT_CAINFO
	parent.add_option("--capath",client.caBundlePath,
	                  "Use the specified certificate directory to verify SSL/TLS connections")
	                  ->envname("CURL_CA_BUNDLE")
	                  ->type_name("PATH");
#endif
}

std::string customError(const CLI::App *app, const CLI::Error &e) {
	std::string header = std::string(e.what()) + "\n";
	auto subcommands = app->get_subcommands();
	if(app->get_help_ptr() != nullptr && !subcommands.empty()) {
		std::string cmd = app->get_name();
		while (!subcommands.empty()) {
			auto command = subcommands.at(0);
			cmd += " " + command->get_name();
			subcommands = command->get_subcommands();
		}
		
		header += "Run command \"" + cmd + "\" with " + app->get_help_ptr()->get_name() + " for more information about using this subcommand.\n";
	} else if (app->get_help_ptr() != nullptr)
		header += "Run " + app->get_name() + " with " + app->get_help_ptr()->get_name() + " for more information about running slate client.\n";
	
	return header;
}

int main(int argc, char* argv[]){
	try{
		Client client;
		
		CLI::App slate("SLATE command line interface");
		slate.require_subcommand();
		slate.failure_message(customError);
		registerVersionCommand(slate,client);
		registerCompletionCommand(slate,client);
		registerGroupCommands(slate,client);
		registerClusterCommands(slate,client);
		registerApplicationCommands(slate,client);
		registerInstanceCommands(slate,client);
		registerSecretCommands(slate,client);
		registerCommonOptions(slate,client);
		registerWhoAmI(slate,client);
		registerUserCommands(slate,client);
		startReaper();
		CLI11_PARSE(slate, argc, argv);
	}
	catch(OperationFailed&){
		return 1;
	}
	catch(std::exception& ex){
		std::cerr << "slate: Exception: " << ex.what() << std::endl;
		return 1;
	}
	catch(...){
		std::cerr << "slate: Exception" << std::endl;
		return 1;
	}
	return 0;
}
