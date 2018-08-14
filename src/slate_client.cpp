#include <memory>

#include "CLI11.hpp"

#include "Client.h"

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
    auto list = parent.add_subcommand("list", "List clusters");
    list->callback([&client](){ client.listClusters(); });
}

void registerClusterCreate(CLI::App& parent, Client& client){
    auto clusterCreateOpt = std::make_shared<ClusterCreateOptions>();
    auto create = parent.add_subcommand("create", "Register a cluster with SLATE");
    create->add_option("cluster-name", clusterCreateOpt->clusterName, "Name of the cluster to create")->required();
	create->add_option("--vo", clusterCreateOpt->voName, "Name of the VO which will own the cluster")->required();
	create->add_option("--kubeconfig", clusterCreateOpt->kubeconfig, "Path to the kubeconfig used for accessing the cluster. "
					   "If not specified, $KUBECONFIG will be used, or ~/kube/config if that variable is not set.");
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
	auto list = parent.add_subcommand("list-allowed", "List VOs allowed access to a cluster");
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

void registerClusterCommands(CLI::App& parent, Client& client){
	auto cluster = parent.add_subcommand("cluster", "Manage SLATE clusters");
	cluster->require_subcommand();
	registerClusterList(*cluster, client);
	registerClusterCreate(*cluster, client);
	registerClusterDelete(*cluster, client);
	registerClusterListAllowed(*cluster, client);
	registerClusterAllowVO(*cluster, client);
	registerClusterDenyVO(*cluster, client);
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
	install->add_option("tag", appOpt->tag, "Tag include in instance name for identification");
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
	info->add_option("instance", instOpt->instanceID, "The ID of the instance");
    info->callback([&client,instOpt](){ client.getInstanceInfo(*instOpt); });
}

void registerInstanceDelete(CLI::App& parent, Client& client){
	auto instOpt = std::make_shared<InstanceOptions>();
    auto info = parent.add_subcommand("delete", "Destroy an application instance");
	info->add_option("instance", instOpt->instanceID, "The ID of the instance");
    info->callback([&client,instOpt](){ client.deleteInstance(*instOpt); });
}

void registerInstanceCommands(CLI::App& parent, Client& client){
	auto inst = parent.add_subcommand("instance", "Manage SLATE application instances");
	inst->require_subcommand();
	registerInstanceList(*inst, client);
	registerInstanceInfo(*inst, client);
	registerInstanceDelete(*inst, client);
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
	create->add_option("--literal", secrCreateOpt->literal, "Key and literal value to add to secret (in the form key=value)")->required();
	
	create->callback([&client,secrCreateOpt](){ client.createSecret(*secrCreateOpt); });
}

void registerSecretDelete(CLI::App& parent, Client& client){
	auto secrDeleteOpt = std::make_shared<SecretOptions>();
	auto del = parent.add_subcommand("delete", "Remove a secret from SLATE");
	del->add_option("secret", secrDeleteOpt->secretID, "ID of the secret to delete")->required();
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
}

int main(int argc, char* argv[]){
	try{
		Client client;
		
		CLI::App slate("SLATE command line interface");
		slate.require_subcommand();
		registerVOCommands(slate,client);
		registerClusterCommands(slate,client);
		registerApplicationCommands(slate,client);
		registerInstanceCommands(slate,client);
		registerSecretCommands(slate,client);
		registerCommonOptions(slate,client);
		
		CLI11_PARSE(slate, argc, argv);
	}
	catch(std::exception& ex){
		std::cerr << "slate-client: Exception: " << ex.what() << std::endl;
		return 1;
	}
	catch(...){
		std::cerr << "slate-client: Exception" << std::endl;
		return 1;
	}
	return 0;
}
