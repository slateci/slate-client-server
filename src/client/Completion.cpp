#include <client/Completion.h>

#include <iostream>
#include <stdexcept>

#include <Utilities.h>
#include "CLI11.hpp"

static const char zshHeader[]=R"--(
autoload -U +X compinit && compinit
autoload -U +X bashcompinit && bashcompinit
)--";

static const char fishCompletion[]=R"--(
set __fish_slate_commands \
  version \
  completion \
  group \
  cluster \
  app \
  instance \
  secret

function __fish_slate_needs_command -d 'Test if slate has yet to receive a command'
  not __fish_seen_subcommand_from $__fish_slate_commands
  return $status
end

# Global arguments
complete -c slate -f \
  -s h -d  'Print help message' -l help -d  'Print help message' -x
complete -c slate -f -n '__fish_slate_needs_command' \
  -l orderBy -r -d 'JSON output column'
complete -c slate -f -n '__fish_slate_needs_command' \
  -l no-format -d 'Do not use ANSI formatting escape sequences'
complete -c slate -f -n '__fish_slate_needs_command' \
  -l width -r -d 'maximum width'
complete -c slate -f -n '__fish_slate_needs_command' \
  -l api-endpoint -r -d 'URL (Env:SLATE_API_ENDPOINT) of SLATE API server'
complete -c slate -n '__fish_slate_needs_command' \
  -l api-endpoint-file -r -d 'file containing URL of SLATE API server'
complete -c slate -n '__fish_slate_needs_command' \
  -l credential-file -r -d 'file containing credentials to be presented'
complete -c slate -f -n '__fish_slate_needs_command' \
  -l output -r -d 'The format in which to print output' \
  -a 'no-headers json jsonpointer jsonpointer-file custom-columns custom-columns-file'
complete -c slate -n '__fish_slate_needs_command' \
  -l capath -r -d 'Use the specified certificate directory'

# Subcommands
complete -c slate -f -n '__fish_slate_needs_command' \
  -a "\
  version\t'Commands for features in alpha' \
  version\t'Print version information' \
  completion\t'Print a shell completion script' \
  group\t'Manage SLATE groups' \
  cluster\t'Manage SLATE clusters' \
  app\t'View and install SLATE applications' \
  instance\t'Manage SLATE application instances' \
  secret\t'Manage SLATE secrets'"


# Version
complete -c slate -f -n '__fish_seen_subcommand_from version; \
    and not __fish_seen_subcommand_from upgrade' \
  -a "upgrade\t'Upgrade to the latest released version'"

complete -c slate -f -n '__fish_seen_subcommand_from version; \
    and __fish_seen_subcommand_from upgrade' \
  -s y -l assumeyes -d "Assume yes to any question"

complete -c slate -f -n '__fish_seen_subcommand_from completion; \
    and not __fish_seen_subcommand_from shell' \
  -a "bash\t'Bash completions'\
  zsh\t'ZSH completions' \
  fish\t'Fish completions'"

set __fish_slate_group_commands \
  list \
  info\
  create\
  update\
  delete\
  list-allowed-clusters

complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and not __fish_seen_subcommand_from $__fish_slate_group_commands' \
  -a "list\t'List groups'\
  info\t'Get information about a group' \
  create\t'Create a new group' \
  update\t'Update one or more of a group\'s properties'\
  delete\t'Destroy a group'\
  list-allowed-clusters\t'List clusters this group can access'"

complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and __fish_seen_subcommand_from list' \
  -l user -d "Show only groups to which you belong"

complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and __fish_seen_subcommand_from create' \
  -l field -d "The field of science"

complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and __fish_seen_subcommand_from update' \
  -l email -d "Contact email"
complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and __fish_seen_subcommand_from update' \
  -l phone -d "Contact phone"
complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and __fish_seen_subcommand_from update' \
  -l field -d "Field of science"
complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and __fish_seen_subcommand_from update' \
  -l desc -d "Group description" \

complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and __fish_seen_subcommand_from delete' \
  -s y -l assumeyes -d "Assume yes to any question"

# Cluster
set __fish_slate_cluster_commands \
  list \
  info \
  create \
  update \
  delete \
  list-allowed-groups \
  allow-group \
  deny-group \
  list-group-allowed-apps \
  allow-group-app \
  deny-group-app \
  ping

complete -c slate -f -n '__fish_seen_subcommand_from cluster; \
    and not __fish_seen_subcommand_from $__fish_slate_cluster_commands' \
  -a  "\
  list\t'List clusters' \
  info\t'Get information about a cluster' \
  create\t'Register a cluster with SLATE' \
  update\t'Update a cluster\'s information' \
  delete\t'Remove a cluster from SLATE' \
  list-allowed-groups\t'List groups allowed access to a cluster' \
  allow-group\t'Grant a group access to a cluster' \
  deny-group\t'Revoke a group\'s access to a cluster' \
  list-group-allowed-apps\t'List applications a group is allowed to use on a cluster' \
  allow-group-app\t'Grant a group permission to use an application on a cluster' \
  deny-group-app\t'Remove a group\'s permission to use an application on a cluster' \
  ping\t'Check whether the platform can connect to a cluster'"

# Cluster list
complete -c slate -f -n '__fish_seen_subcommand_from cluster; \
    and __fish_seen_subcommand_from list create update' \
  -l group -r -d "The clusters this group is allowed in"

# Cluster create
complete -c slate -f -n '__fish_seen_subcommand_from cluster; \
    and __fish_seen_subcommand_from create update' \
  -l org -f -r -d "Organization which owns the hardware"
complete -c slate -f -n '__fish_seen_subcommand_from cluster; \
    and __fish_seen_subcommand_from create update' \
  -l kubeconfig -r -d "path of kubeconfig"
complete -c slate -f -n '__fish_seen_subcommand_from cluster; \
    and __fish_seen_subcommand_from create update delete' \
  -s y -l assumeyes -d "Assume yes to any question"

# Cluster Update
complete -c slate -f -n '__fish_seen_subcommand_from cluster; \
    and __fish_seen_subcommand_from update' \
  -s r -l reconfigure -d "Update the kubeconfig"
complete -c slate -f -n '__fish_seen_subcommand_from cluster; \
    and __fish_seen_subcommand_from update' \
  -l location -d "Geographic location (lat,lon)"

# Cluster delete
complete -c slate -f -n '__fish_seen_subcommand_from cluster; \
    and __fish_seen_subcommand_from delete' \
  -s f -l force -d "Force deletion"

set __fish_slate_app_commands \
  list \
  get-conf\
  info \
  install

complete -c slate -f -n '__fish_seen_subcommand_from app; \
    and not __fish_seen_subcommand_from $__fish_slate_app_commands' \
  -a  "\
  list\t'List available applications' \
  get-conf\t'Get the configuration template for an application' \
  info\t'Get an application\'s documentation' \
  install\t'Install an instance of an application'"

complete -c slate -f -n '__fish_seen_subcommand_from app; \
    and __fish_seen_subcommand_from list get-conf info install' \
  -l dev -r -d "Consider development catalog"

complete -c slate -f -n '__fish_seen_subcommand_from app; \
    and __fish_seen_subcommand_from get-conf info' \
  -s o -l output -r -d "File to which to write configuration"


complete -c slate -f -n '__fish_seen_subcommand_from app; \
    and __fish_seen_subcommand_from group' \
  -l group -r -d "Name of group which will own the instance"
complete -c slate -f -n '__fish_seen_subcommand_from app; \
    and __fish_seen_subcommand_from group' \
  -l cluster -r -d "Name of cluster on which the instance will run"
complete -c slate -f -n '__fish_seen_subcommand_from app; \
    and __fish_seen_subcommand_from group' \
  -l conf -r -d "File containing configuration"
complete -c slate -f -n '__fish_seen_subcommand_from app; \
    and __fish_seen_subcommand_from group' \
  -l local -r -d "Install a local chart"


set __fish_slate_instance_commands \
  list \
  info \
  restart \
  delete \
  logs \
  scale

complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and not __fish_seen_subcommand_from $__fish_slate_instance_commands' \
  -a  "\
  list\t'List deployed application instances' \
  info\t'Fetch information about a deployed instance' \
  restart\t'Stop and restart a deployed instance' \
  delete\t'Destroy an application instance' \
  logs\t'Get logs from an application instance' \
  scale\t'View or set the number of replicas of an instance'"

complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from list' \
  -l group -r -d "Show only instances belonging to this group"
complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from list' \
  -l cluster -r -d "Show only instances running on this cluster"

complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from delete' \
  -s y -l assumeyes -d "Assume yes to any confirmation"
complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from delete' \
  -s f -l force -d "Force deletion"

complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from logs' \
  -l max-lines -f -r -d "Maximum number to fetch, 0 for full logs"

complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from logs' \
  -l container -f -r -d "Name of specific container"

complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from logs' \
  -l previous -f -r -d "Name of specific container"

complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from scale' \
  -l replicas -f -r -d "Integer number of replicas to scale"
complete -c slate -f -n '__fish_seen_subcommand_from instance; \
    and __fish_seen_subcommand_from scale' \
  -l deployment -f -r -d "The deployment to operate on"

set __fish_slate_secret_commands \
  list \
  info \
  create \
  copy \
  delete

complete -c slate -f -n '__fish_seen_subcommand_from secret; \
    and not __fish_seen_subcommand_from $__fish_slate_secret_commands' \
  -a  "\
  list\t'List secrets' \
  info\t'Fetch information about a secret' \
  create\t'Create a new secret' \
  copy\t'Copy a secret to another cluster' \
  delete\t'Remove a secret from SLATE'"

complete -c slate -f -n '__fish_seen_subcommand_from secret; \
    and __fish_seen_subcommand_from list create copy' \
  -l group -r -d "Group on which to operate"
complete -c slate -f -n '__fish_seen_subcommand_from secret; \
    and __fish_seen_subcommand_from list create copy' \
  -l cluster -r -d "Cluster on which to operate"

complete -c slate -f -n '__fish_seen_subcommand_from secret; \
    and __fish_seen_subcommand_from create' \
  -l from-literal -r -d "Key and literal value to add to secret"
complete -c slate -f -f -n '__fish_seen_subcommand_from secret; \
    and __fish_seen_subcommand_from create' \
  -l from-file -r -d "Filename to use as key with file contents as value"
complete -c slate -f -f -n '__fish_seen_subcommand_from secret; \
    and __fish_seen_subcommand_from create' \
  -l from-env-file -r -d "Path to a file from which to read key value pairs"
complete -c slate -f -n '__fish_seen_subcommand_from secret; \
    and __fish_seen_subcommand_from delete' \
  -s y -l assumeyes -d "Assume yes to any confirmation"
complete -c slate -f -n '__fish_seen_subcommand_from secret; \
    and __fish_seen_subcommand_from delete' \
  -s f -l force -d "Force deletion"
)--";

void optsWithArgs(const CLI::App& comm, std::vector<std::string>& vec) {
	for(const auto opt : comm.get_options([](const CLI::Option* opt){return opt->get_type_size();})){
		vec.push_back(opt->get_name());
	}
	for(const auto subcomm : comm.get_subcommands([](const CLI::App*){return true;}))
		optsWithArgs(*subcomm,vec);
}

void completions_rec(const CLI::App& comm, std::string indent="", unsigned int depth=0){
	// First, we want to generate our completion reply "at the current place" if appropriate
	auto opts = comm.get_options(
			[](const CLI::Option* opt){return !opt->get_positional();
	});
	auto comms = comm.get_subcommands([](const CLI::App*){return true;});
	std::cout << indent << R"(if [ "$nNonOptions" -eq )"
	                    << depth
	                    << R"( ]; then)" << std::endl
	          << indent << "\t" << R"(COMPREPLY=($(compgen -W ")";
	if (!comms.empty()) {
		for (const auto comm : comms)
			std::cout << comm->get_name() << " ";
	}
	for (auto iter = opts.begin(); iter != opts.end(); iter++) {
		if (iter != opts.begin()) std::cout << " ";
		std::cout << (*iter)->get_name();
	}
	std::cout << R"(" -- "${COMP_WORDS[$COMP_CWORD]}")))" << std::endl
	          << indent << "\t" << "return;" << std::endl
	          << indent << "fi" << std::endl;

	// Next, we try each subcommand
	if (comms.empty()) {
		return;
	}

	std::cout << indent << R"(case "${subcommands[)" << depth << R"(]}" in)" << std::endl;
	for(const auto subcomm : comms) {
		std::cout << indent << "\t" << subcomm->get_name() << ")" << std::endl;
		completions_rec(*subcomm,indent+"\t\t",depth+1);
		std::cout << indent << "\t\t"<< ";;" << std::endl;
	}
	std::cout << indent << R"(esac)" << std::endl;
}

void getCompletionScript(const CLI::App& comm, std::string shell){
	if(shell.empty()){
		fetchFromEnvironment("SHELL",shell);
		if(shell.empty())
			throw std::runtime_error("$SHELL is not set");
	}

	if(shell=="bash" || (shell.find("/bash")==shell.size()-5 &&
				shell.size() > 5)){
		// function header and argument-taking options
		std::vector<std::string> opts;
		optsWithArgs(comm, opts);
		std::cout << "_slate_completions(){" << std::endl
		          << "\t" << "local optionsWithArgs=\"";
		for (auto iter = opts.begin(); iter != opts.end(); iter++) {
			if (iter != opts.begin()) std::cout << " ";
			std::cout << *iter;
		}
		std::cout << '"' << std::endl;
		// Perform parsing
		std::cout << R"--(
	# count non-option arguments so far to figure out where we are
	local nNonOptions=0
	local nOptions=0
	local optionConsumesNext=""
	local foundSlate=""
	for i in $(seq 0 $((COMP_CWORD - 1))); do
		# if someone is writing a complex command, other words may precede the slate program,
		# and we need to ignore them
		if [ -z "$foundSlate" ]; then
			if echo "${COMP_WORDS[$i]}" | grep -q 'slate$'; then
				foundSlate=1
			fi
			continue
		fi

		if echo "${COMP_WORDS[$i]}" | grep -q '^-'; then
			nOptions=$((nOptions + 1))
			if echo "$optionsWithArgs" | grep -q  -- "${COMP_WORDS[$i]}" - ; then
				optionConsumesNext=1
			fi
		else
			if [ -n "${optionConsumesNext}" ]; then
				optionConsumesNext=""
			elif [ "${COMP_WORDS[$i]}" ]; then
				subcommands[$nNonOptions]="${COMP_WORDS[$i]}"
				nNonOptions=$(expr $nNonOptions + 1)
			fi
		fi
	done)--" << std::endl;
		// next, output our tree of subcommands
		completions_rec(comm, "\t");
		// finally output the end of the function and enable completions
		std::cout << "}" << std::endl
		          << "complete -o default -F _slate_completions slate" << std::endl;

		return;
	}

	if(shell=="zsh" || (shell.find("/zsh")==shell.size()-4 &&
				shell.size() > 4)){
		std::cout << zshHeader;
		getCompletionScript(comm, "bash");
		return;
	}
	if(shell=="fish" || (shell.find("/fish")==shell.size()-5 &&
				shell.size() > 5)){
		std::cout << fishCompletion << std::endl;
		return;
	}

	throw std::runtime_error("Unsupported or unrecoginzed shell for completions: "+shell);
}
