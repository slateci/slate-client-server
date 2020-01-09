#include <client/Completion.h>

#include <iostream>
#include <stdexcept>

#include <Utilities.h>

//#!/usr/bin/env bash
//Reading this data from a file would be nicer, but we need to be completely self-contained.
static const char bashCompletion[]=R"--(
_slate_completions(){
	local optionsWithArgs
	optionsWithArgs="--width --api-endpoint --api-endpoint-file --credential-file \
	                 --output --group --cluster --kubeconfig --conf --from-literal \
	                 --from-file --from-env-file --orderBy --email --phone --field \
	                 --desc --org --location --replicas --deployment"

	# count non-option arguments so far to figure out where we are
	local nNonOptions=0
	local nOptions=0
	local optionConsumesNext=""
	local foundSlate=""
	for i in $(seq 0 $(expr $COMP_CWORD - 1)); do
		# echo "Index $i: (${COMP_WORDS[$i]})"

		# if someone is writing a complex command, other words may precede the slate program,
		# and we need to ignore them
		if [ -z "$foundSlate" ]; then
			if echo "${COMP_WORDS[$i]}" | grep -q 'slate$'; then
				#echo "found slate; start counting arguments and options"
				foundSlate=1
			else
				: #echo "haven't yet found slate"
			fi
			continue
		fi

		if echo "${COMP_WORDS[$i]}" | grep -q '^-'; then
			#echo "${COMP_WORDS[$i]} is an option"
			nOptions=$(expr $nOptions + 1)
			#if [ -n "optionsWithArgs[${COMP_WORDS[$i]}]" ]; then
			if echo "$optionsWithArgs" | grep -q  -- "${COMP_WORDS[$i]}" - ; then
				optionConsumesNext=1
				#echo "${COMP_WORDS[$i]} consumes next argument"
			fi
		else
			if [ -n "${optionConsumesNext}" ]; then
				#echo "${COMP_WORDS[$i]} consumed by previous option"
				optionConsumesNext=""
			elif [ "${COMP_WORDS[$i]}" ]; then
				#echo "${COMP_WORDS[$i]} is not an option"
				subcommands[$nNonOptions]="${COMP_WORDS[$i]}"
				nNonOptions=$(expr $nNonOptions + 1)
			else
				: #echo "the empty string is not an argument"
			fi
		fi
	done

	# first level subcommand or general options
	if [ "$nNonOptions" -eq 0 ]; then
		COMPREPLY=($(compgen -W "group cluster app instance secret version completion \
		                         --help --no-format --width --api-endpoint \
		                         --api-endpoint-file --credential-file --output \
		                         --orderBy" \
		                     -- "${COMP_WORDS[$COMP_CWORD]}"))
	fi

	# second level subcommand or option to first level subcommand
	if [ "$nNonOptions" -eq 1 ]; then
		if [ "${subcommands[0]}" = "group" ]; then
			COMPREPLY=($(compgen -W "--help list info create update delete" -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "cluster" ]; then
			COMPREPLY=($(compgen -W "--help list info create update delete list-allowed-groups \
			                         allow-group deny-group list-group-allowed-apps \
			                         allow-group-app deny-group-app" \
			                     -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "app" ]; then
			COMPREPLY=($(compgen -W "--help list info get-conf install" -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "instance" ]; then
			COMPREPLY=($(compgen -W "--help list info restart delete logs scale" -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "secret" ]; then
			COMPREPLY=($(compgen -W "--help list info create copy delete" -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "version" ]; then
			COMPREPLY=($(compgen -W "--help upgrade" -- "${COMP_WORDS[$COMP_CWORD]}"))
		fi
	fi

	# option or argument to second level subcommand
	if [ "$nNonOptions" -ge 2 ]; then
		if [ "${subcommands[0]}" = "group" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --user" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "info" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "create" ]; then
				COMPREPLY=($(compgen -W "--help --field" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "update" ]; then
				COMPREPLY=($(compgen -W "--help --email --phone --field --desc" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "delete" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "cluster" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --group" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "info" ]; then
				COMPREPLY=($(compgen -f -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "create" ]; then
				COMPREPLY=($(compgen -f -W "--help --group --kubeconfig -y --assumeyes" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "update" ]; then
				COMPREPLY=($(compgen -f -W "--help --org --r --reconfigure --kubeconfig -y --assumeyes --location" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "delete" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "list-allowed-groups" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "allow-group" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "deny-group" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "list-group-allowed-apps" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "allow-group-app" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "deny-group-app" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "app" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --dev" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "info" ]; then
				COMPREPLY=($(compgen -W "--help --output --dev" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "get-conf" ]; then
				COMPREPLY=($(compgen -W "--help --output --dev" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "install" ]; then
				COMPREPLY=($(compgen -f -W "--help --group --cluster --conf --dev" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "instance" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --group --cluster" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "info" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "delete" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "logs" ]; then
				COMPREPLY=($(compgen -W "--help --max-lines --container" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "restart" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "scale" ]; then
				COMPREPLY=($(compgen -W "--help --replicas --deployment" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "secret" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --group --cluster" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "info" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "create" ]; then
				COMPREPLY=($(compgen -f -W "--help --group --cluster --from-literal --from-file --from-env-file" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "copy" ]; then
				COMPREPLY=($(compgen -f -W "--help --group --cluster" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "delete" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "version" ]; then
			if [ "${subcommands[1]}" = "upgrade" ]; then
				COMPREPLY=($(compgen -W "--help -y --assumeyes" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		fi
	fi
}

complete -o default -F _slate_completions slate
)--";

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
  delete

complete -c slate -f -n '__fish_seen_subcommand_from group; \
    and not __fish_seen_subcommand_from $__fish_slate_group_commands' \
  -a "list\t'List groups'\
  info\t'Get information about a group' \
  create\t'Create a new group' \
  update\t'Update one or more of a group\'s properties'\
  delete\t'Destroy a group'"

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

void getCompletionScript(std::string shell){
	if(shell.empty()){
		fetchFromEnvironment("SHELL",shell);
		if(shell.empty())
			throw std::runtime_error("$SHELL is not set");
	}

	if(shell=="bash" || (shell.find("/bash")==shell.size()-5 &&
				shell.size() > 5)){
		std::cout << bashCompletion << std::endl;
		return;
	}

	if(shell=="zsh" || (shell.find("/zsh")==shell.size()-4 &&
				shell.size() > 4)){
		std::cout << zshHeader << bashCompletion << std::endl;
		return;
	}
	if(shell=="fish" || (shell.find("/fish")==shell.size()-5 &&
				shell.size() > 5)){
		std::cout << fishCompletion << std::endl;
		return;
	}

	throw std::runtime_error("Unsupported or unrecoginzed shell for completions: "+shell);
}
