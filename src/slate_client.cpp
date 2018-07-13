#include <memory>

#include "CLI11.hpp"

#include "Client.h"

void registerVOList(CLI::App& parent, Client& client){
    auto list = parent.add_subcommand("list", "List VOs");
    list->callback([&client](){ client.listVOs(); });
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

void registerClusterCommands(CLI::App& parent, Client& client){
	auto cluster = parent.add_subcommand("cluster", "Manage SLATE clusters");
	cluster->require_subcommand();
	registerClusterList(*cluster, client);
	registerClusterCreate(*cluster, client);
	registerClusterDelete(*cluster, client);
}

void registerApplicationList(CLI::App& parent, Client& client){
	auto appOpt = std::make_shared<ApplicationOptions>();
    auto list = parent.add_subcommand("list", "List available applications");
	list->add_flag("--dev", appOpt->devRepo, "Show applications from the development catalog");
    list->callback([&client,appOpt](){ client.listApplications(*appOpt); });
}

void registerApplicationGetConf(CLI::App& parent, Client& client){
	auto appOpt = std::make_shared<ApplicationConfOptions>();
    auto conf = parent.add_subcommand("get-conf", "Get the configuration template for an application");
	conf->add_option("app-name", appOpt->appName, "Name of the application to fetch")->required();
	conf->add_option("-o,--output", appOpt->outputFile, "File to which to write the configuration");
	conf->add_flag("--dev", appOpt->devRepo, "Fetch from the development catalog");
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
	auto inst = parent.add_subcommand("instance", "Manage SLATE application instance");
	inst->require_subcommand();
	registerInstanceList(*inst, client);
	registerInstanceInfo(*inst, client);
	registerInstanceDelete(*inst, client);
}

void registerCommonOptions(CLI::App& parent, Client& client){
	parent.add_flag_function("--no-format", 
	                         [&](std::size_t){ client.setUseANSICodes(false); }, 
	                         "Do not use ANSI formatting esacpe sequences in output");
	parent.add_option("--width",client.outputWidth,
	                  "The maximum width to use when printing tabular output");
	parent.add_option("--api-endpoint",client.apiEndpoint,
	                  "The endpoint at which to contact the SLATE API server")
	                 ->envname("SLATE_API_ENDPOINT");
}

int main(int argc, char* argv[]){
	Client client;
	
	CLI::App slate("SLATE command line interface");
	slate.require_subcommand();
	registerVOCommands(slate,client);
	registerClusterCommands(slate,client);
	registerApplicationCommands(slate,client);
	registerInstanceCommands(slate,client);
	registerCommonOptions(slate,client);
	
	CLI11_PARSE(slate, argc, argv);
}
