#include <Completion.h>

#include <iostream>
#include <stdexcept>

#include <Utilities.h>

//#!/usr/bin/env bash
//Reading this data from a file would be nicer, but we need to be completely self-contained.
static const char bashCompletion[]=R"--(
_slate_completions(){
	local optionsWithArgs
	optionsWithArgs="--width --api-endpoint --api-endpoint-file --credential-file --output --vo --cluster --kubeconfig --conf --from-literal --from-file --from-env-file"
	
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
			if echo "$optionsWithArgs" | grep -q "${COMP_WORDS[$i]}" - ; then
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
		COMPREPLY=($(compgen -W "vo cluster app instance secret --help --no-format --width \
		                         --api-endpoint --api-endpoint-file --credential-file --output \
		                         version completion" \
		                     -- "${COMP_WORDS[$COMP_CWORD]}"))
	fi
	
	# second level subcommand or option to first level subcommand
	if [ "$nNonOptions" -eq 1 ]; then
		if [ "${subcommands[0]}" = "vo" ]; then
			COMPREPLY=($(compgen -W "--help list create delete" -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "cluster" ]; then
			COMPREPLY=($(compgen -W "--help list create delete list-allowed allow-vo deny-vo" \
			                     -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "app" ]; then
			COMPREPLY=($(compgen -W "--help list get-conf install" -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "instance" ]; then
			COMPREPLY=($(compgen -W "--help list info delete logs" -- "${COMP_WORDS[$COMP_CWORD]}"))
		elif [ "${subcommands[0]}" = "secret" ]; then
			COMPREPLY=($(compgen -W "--help list info create delete" -- "${COMP_WORDS[$COMP_CWORD]}"))
		fi
	fi
	
	# option or argument to second level subcommand
	if [ "$nNonOptions" -ge 2 ]; then
		if [ "${subcommands[0]}" = "vo" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --user" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "create" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "delete" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "cluster" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "create" ]; then
				COMPREPLY=($(compgen -f -W "--help --vo --kubeconfig -y --assumeyes" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "delete" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "list-allowed" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "allow-vo" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "deny-vo" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "app" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --dev" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "get-conf" ]; then
				COMPREPLY=($(compgen -W "--help --output --dev" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "install" ]; then
				COMPREPLY=($(compgen -f -W "--help --vo --cluster --conf --dev" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "instance" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --vo --cluster" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "info" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "delete" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "logs" ]; then
				COMPREPLY=($(compgen -W "--help --max-lines --container" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		elif [ "${subcommands[0]}" = "secret" ]; then
			if [ "${subcommands[1]}" = "list" ]; then
				COMPREPLY=($(compgen -W "--help --vo --cluster" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "info" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "create" ]; then
				COMPREPLY=($(compgen -f -W "--help --vo --cluster --from-literal --from-file --from-env-file" -- "${COMP_WORDS[$COMP_CWORD]}"))
			elif [ "${subcommands[1]}" = "delete" ]; then
				COMPREPLY=($(compgen -W "--help" -- "${COMP_WORDS[$COMP_CWORD]}"))
			fi
		fi
	fi
}

complete -o default -F _slate_completions slate
)--";

static const char zshHeader[]=R"--(
__slate_bash_source() {
	alias shopt=':'
	alias _expand=_bash_expand
	alias _complete=_bash_comp
	emulate -L sh
	setopt kshglob noshglob braceexpand
	source "$@"
}

__slate_declare() {
	builtin declare "$@"
}

__slate_compgen(){
	echo "__slate_compgen , @=$@, 1=$1"
	local completions w
	completions=( $(compgen "$@") ) || return $?
	# filter by given word as prefix
	while [ "$1" = -* && "$1" != -- ]; do
		shift
		shift
        done
        if [ "$1" == -- ]; then
	shift
	fi
	for w in "${completions[@]}"; do
		if [ "${w}" = "$1"* ]; then
	echo "${w}"
	fi
	done
}

__slate_convert_bash_to_zsh(){
	sed \
	-e 's/local \([a-zA-Z0-9_]*\)=/local \1; \1=/' \
	-e "s/compgen/__slate_compgen/g" \
	-e "s/declare/__slate_declare/g" \
	-e "s/complete -o default -F/compdef/g" \
	<<'BASH_COMPLETION_EOF'
)--";

static const char zshFooter[]=R"--(
BASH_COMPLETION_EOF
}
__slate_bash_source <(__slate_convert_bash_to_zsh)
)--";

void getCompletionScript(std::string shell){
	if(shell.empty()){
		fetchFromEnvironment("SHELL",shell);
		if(shell.empty())
			throw std::runtime_error("$SHELL is not set");
	}
	
	if(shell=="bash" || shell.find("/bash")==shell.size()-5){
		std::cout << bashCompletion << std::endl;
		return;
	}
	
	//DOES NOT CURRENTLY WORK
	/*if(shell=="zsh" || shell.find("/zsh")==shell.size()-4){
		std::cout << zshHeader << bashCompletion << zshFooter << std::endl;
		return;
	}*/
	
	throw std::runtime_error("Unsupported or unrecoginzed shell for completions: "+shell);
}