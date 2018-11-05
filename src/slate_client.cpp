#include <memory>

#include "CLI11.hpp"

#include "Client.h"
#include "SecretLoading.h"
#include "Process.h"
#include "Completion.h"

void registerVersionCommand(CLI::App& parent, Client& client){
	auto version = parent.add_subcommand("version", "Print version information");
	version->callback([&client](){ client.printVersion(); });
}

void registerCompletionCommand(CLI::App& parent, Client& client){
	auto shell = std::make_shared<std::string>();
	auto completion = parent.add_subcommand("completion", "Print a shell completion script");
	completion->add_option("shell", *shell, "The shell for which to produce a completion script")->envname("SHELL");
	completion->callback([shell](){ getCompletionScript(*shell); });
}

void registerVOList(CLI::App& parent, Client& client){
    auto voListOpt = std::make_shared<VOListOptions>();
    auto list = parent.add_subcommand("list", "List VOs");
    list->callback([&client, voListOpt](){ client.listVOs(*voListOpt); });
    list->add_flag("--user", voListOpt->user, "Show only VOs to which you belong"); 
}

void registerVOCreate(CLI::App& parent, Client& client){
    auto voCreateOpt = std::make_shared<VOCreateOptions>();
    auto create = parent.add_subcommand("create", "Create a new VO");
    create->add_option("vo-name", voCreateOpt->voName, "Name of the vo to create")->required();
    create->callback([&client,voCreateOpt](){ client.createVO(*voCreateOpt); });
}

void registerVODelete(CLI::App& parent, Client& client){
    auto voDeleteOpt = std::make_shared<VODeleteOptions>();
    auto del = parent.add_subcommand("delete", "Destroy a VO");
    del->add_option("vo-name", voDeleteOpt->voName, "Name of the vo to delete")->required();
    del->callback([&client,voDeleteOpt](){ client.deleteVO(*voDeleteOpt); });
}

void registerVOCommands(CLI::App& parent, Client& client){
	auto vo = parent.add_subcommand("vo", "Manage SLATE VOs");
	vo->require_subcommand();
	registerVOList(*vo, client);
	registerVOCreate(*vo, client);
	registerVODelete(*vo, client);
}

void registerClusterList(CLI::App& parent, Client& client){
    auto clusterListOpt = std::make_shared<ClusterListOptions>();
    auto list = parent.add_subcommand("list", "List clusters");
    list->add_option("--vo", clusterListOpt->vo, "Show only clusters this vo is allowed on");
    list->callback([&client, clusterListOpt](){ client.listClusters(*clusterListOpt); });
}

void registerClusterCreate(CLI::App& parent, Client& client){
    auto clusterCreateOpt = std::make_shared<ClusterCreateOptions>();
    auto create = parent.add_subcommand("create", "Register a cluster with SLATE");
    create->add_option("cluster-name", clusterCreateOpt->clusterName, "Name of the cluster to create")->required();
	create->add_option("--vo", clusterCreateOpt->voName, "Name of the VO which will own the cluster")->required();
	create->add_option("--kubeconfig", clusterCreateOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					   "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set.");
	create->add_flag("-y,--assumeyes", clusterCreateOpt->assumeYes, "Assume yes, or the default answer, to any question which would be asked");
    create->callback([&client,clusterCreateOpt](){ client.createCluster(*clusterCreateOpt); });
}

void registerClusterDelete(CLI::App& parent, Client& client){
    auto clusterDeleteOpt = std::make_shared<ClusterDeleteOptions>();
    auto del = parent.add_subcommand("delete", "Remove a cluster from SLATE");
    del->add_option("cluster-name", clusterDeleteOpt->clusterName, "Name of the cluster to delete")->required();
    del->callback([&client,clusterDeleteOpt](){ client.deleteCluster(*clusterDeleteOpt); });
}

void registerClusterListAllowed(CLI::App& parent, Client& client){
	auto accessOpt = std::make_shared<ClusterAccessListOptions>();
	auto list = parent.add_subcommand("list-allowed-vos", "List VOs allowed access to a cluster");
	list->add_option("cluster-name", accessOpt->clusterName, "Name of the cluster")->required();
	list->callback([&client,accessOpt](){ client.listVOWithAccessToCluster(*accessOpt); });
}

void registerClusterAllowVO(CLI::App& parent, Client& client){
	auto accessOpt = std::make_shared<VOClusterAccessOptions>();
	auto allow = parent.add_subcommand("allow-vo", "Grant a VO access to a cluster");
	allow->add_option("cluster-name", accessOpt->clusterName, "Name of the cluster to give access to")->required();
	allow->add_option("vo-name", accessOpt->voName, "Name of the VO to give access")->required();
	allow->callback([&client,accessOpt](){ client.grantVOClusterAccess(*accessOpt); });
}

void registerClusterDenyVO(CLI::App& parent, Client& client){
	auto accessOpt = std::make_shared<VOClusterAccessOptions>();
	auto deny = parent.add_subcommand("deny-vo", "Revoke a VO's access to a cluster");
	deny->add_option("cluster-name", accessOpt->clusterName, "Name of the cluster to remove access to")->required();
	deny->add_option("vo-name", accessOpt->voName, "Name of the VO whose access to remove")->required();
	deny->callback([&client,accessOpt](){ client.revokeVOClusterAccess(*accessOpt); });
}

void registerListAllowedApplications(CLI::App& parent, Client& client){
	auto listOpt = std::make_shared<VOClusterAppUseListOptions>();
	auto list = parent.add_subcommand("list-vo-allowed-apps", "List applications a VO is allowed to use on a cluster");
	list->add_option("cluster-name", listOpt->clusterName, "Name of the cluster")->required();
	list->add_option("vo-name", listOpt->voName, "Name of the VO")->required();
	list->callback([&client,listOpt](){ client.listAllowedApplications(*listOpt); });
}

void registerAllowVOUseOfApplication(CLI::App& parent, Client& client){
	auto useOpt = std::make_shared<VOClusterAppUseOptions>();
	auto allow = parent.add_subcommand("allow-vo-app", "Grant a VO permission to use an application on a cluster");
	allow->add_option("cluster-name", useOpt->clusterName, "Name of the cluster")->required();
	allow->add_option("vo-name", useOpt->voName, "Name of the VO")->required();
	allow->add_option("app-name", useOpt->appName, "Name of the application")->required();
	allow->callback([&client,useOpt](){ client.allowVOUseOfApplication(*useOpt); });
}

void registerDenyVOUseOfApplication(CLI::App& parent, Client& client){
	auto useOpt = std::make_shared<VOClusterAppUseOptions>();
	auto allow = parent.add_subcommand("deny-vo-app", "Remove a VO's permission to use an application on a cluster");
	allow->add_option("cluster-name", useOpt->clusterName, "Name of the cluster")->required();
	allow->add_option("vo-name", useOpt->voName, "Name of the VO")->required();
	allow->add_option("app-name", useOpt->appName, "Name of the application")->required();
	allow->callback([&client,useOpt](){ client.denyVOUseOfApplication(*useOpt); });
}

void registerClusterCommands(CLI::App& parent, Client& client){
	auto cluster = parent.add_subcommand("cluster", "Manage SLATE clusters");
	cluster->require_subcommand();
	registerClusterList(*cluster, client);
	registerClusterCreate(*cluster, client);
	registerClusterDelete(*cluster, client);
	registerClusterListAllowed(*cluster, client);
	registerClusterAllowVO(*cluster, client);
	registerClusterDenyVO(*cluster, client);
	registerListAllowedApplications(*cluster, client);
	registerAllowVOUseOfApplication(*cluster, client);
	registerDenyVOUseOfApplication(*cluster, client);
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

void registerApplicationInstall(CLI::App& parent, Client& client){
	auto appOpt = std::make_shared<ApplicationInstallOptions>();
    auto install = parent.add_subcommand("install", "Install an instance of an application");
	install->add_option("app-name", appOpt->appName, "Name of the application to install")->required();
	install->add_option("--vo", appOpt->vo, "Name of the VO which will own the instance")->required();
	install->add_option("--cluster", appOpt->cluster, "Name of the cluster on which the instance will run")->required();
	install->add_option("--conf", appOpt->configPath, "File containing configuration for the instance");
	install->add_flag("--dev", appOpt->devRepo, "Install from the development catalog");
	install->add_flag("--test", appOpt->testRepo, "Install from the test catalog")->group("");
    install->callback([&client,appOpt](){ client.installApplication(*appOpt); });
}

void registerApplicationCommands(CLI::App& parent, Client& client){
	auto app = parent.add_subcommand("app", "View and install SLATE applications");
	app->require_subcommand();
	registerApplicationList(*app, client);
	registerApplicationGetConf(*app, client);
	registerApplicationInstall(*app, client);
}

void registerInstanceList(CLI::App& parent, Client& client){
	auto instOpt = std::make_shared<InstanceListOptions>();
    auto list = parent.add_subcommand("list", "List deployed application instances");
	list->add_option("--vo", instOpt->vo, "Show only instances belonging to this vo");
	list->add_option("--cluster", instOpt->cluster, "Show only instances running on this cluster");
    list->callback([&client,instOpt](){ client.listInstances(*instOpt); });
}

void registerInstanceInfo(CLI::App& parent, Client& client){
	auto instOpt = std::make_shared<InstanceOptions>();
    auto info = parent.add_subcommand("info", "Fetch information about a deployed instance");
	info->add_option("instance", instOpt->instanceID, "The ID of the instance")->required();
    info->callback([&client,instOpt](){ client.getInstanceInfo(*instOpt); });
}

void registerInstanceDelete(CLI::App& parent, Client& client){
	auto delOpt = std::make_shared<InstanceDeleteOptions>();
    auto info = parent.add_subcommand("delete", "Destroy an application instance");
	info->add_option("instance", delOpt->instanceID, "The ID of the instance")->required();
	info->add_flag("--force", delOpt->force, "Force deletion even if helm cannot "
	                 "delete the instance from the kubernetes cluster. Use with caution, "
	                 "as this can potentially leave a running, but undeletable deployment.");
    info->callback([&client,delOpt](){ client.deleteInstance(*delOpt); });
}

void registerInstanceFetchLogs(CLI::App& parent, Client& client){
	auto instOpt = std::make_shared<InstanceLogOptions>();
    auto info = parent.add_subcommand("logs", "Get logs from an application instance");
	info->add_option("instance", instOpt->instanceID, "The ID of the instance")->required();
	info->add_option("--max-lines", instOpt->maxLines, "Maximum number of most recent lines to fetch, 0 to get full logs");
	info->add_option("--container", instOpt->container, "Name of specific container for which to fetch logs");
    info->callback([&client,instOpt](){ client.fetchInstanceLogs(*instOpt); });
}

void registerInstanceCommands(CLI::App& parent, Client& client){
	auto inst = parent.add_subcommand("instance", "Manage SLATE application instances");
	inst->require_subcommand();
	registerInstanceList(*inst, client);
	registerInstanceInfo(*inst, client);
	registerInstanceDelete(*inst, client);
	registerInstanceFetchLogs(*inst, client);
}

void registerSecretList(CLI::App& parent, Client& client){
	auto secrOpt = std::make_shared<SecretListOptions>();
	auto list = parent.add_subcommand("list", "List secrets");
	list->add_option("--vo", secrOpt->vo, "Show only secrets belonging to this vo")->required();
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
	create->add_option("--vo", secrCreateOpt->vo, "VO to create secret on")->required();
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

void registerSecretDelete(CLI::App& parent, Client& client){
	auto secrDeleteOpt = std::make_shared<SecretDeleteOptions>();
	auto del = parent.add_subcommand("delete", "Remove a secret from SLATE");
	del->add_option("secret", secrDeleteOpt->secretID, "ID of the secret to delete")->required();
	del->add_flag("--force", secrDeleteOpt->force, "Force deletion even if the secret "
	                 "cannot be deleted from the kubernetes cluster. Use with caution, "
	                 "as this can potentially leave an existing, but invisible secret.");
	del->callback([&client,secrDeleteOpt](){ client.deleteSecret(*secrDeleteOpt); });
}

void registerSecretCommands(CLI::App& parent, Client& client){
	auto secr = parent.add_subcommand("secret", "Manage SLATE secrets");
	secr->require_subcommand();
	registerSecretList(*secr, client);
	registerSecretInfo(*secr, client);
	registerSecretCreate(*secr, client);
	registerSecretDelete(*secr, client);
}

void registerCommonOptions(CLI::App& parent, Client& client){
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
		slate.failure_message(*customError);
		registerVersionCommand(slate,client);
		registerCompletionCommand(slate,client);
		registerVOCommands(slate,client);
		registerClusterCommands(slate,client);
		registerApplicationCommands(slate,client);
		registerInstanceCommands(slate,client);
		registerSecretCommands(slate,client);
		registerCommonOptions(slate,client);
		
		startReaper();
		CLI11_PARSE(slate, argc, argv);
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
