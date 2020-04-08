Table of Contents
=================

1. [Installing](#installing)
1. [Usage](#usage)
   1. [Configuration](#configuration)
   1. [General](#general)
      1. [--help](#--help)
      1. [--output](#--output)
      1. [--no-format](#--no-format)
      1. [version](#version)
      1. [version upgrade](#version-upgrade)
      1. [Completions](#completions)
          1. [Bash](#Bash)
          1. [Bash (macOS/Homebrew)](#Bash-macOSHomebrew)
          1. [Fish](#Fish)
          1. [Zsh](#Zsh)
   1. [Group Commands](#group-commands)
      1. [group list](#group-list)
      1. [group info](#group-info)
      1. [group create](#group-create)
      1. [group update](#group-update)
      1. [group delete](#group-delete)
      1. [group list-allowed-clusters](#group-list-allowed-clusters)
   1. [Cluster Commands](#cluster-commands)
      1. [cluster list](#cluster-list)
      1. [cluster info](#cluster-info)
      1. [cluster create](#cluster-create)
      1. [cluster update](#cluster-update)
      1. [cluster delete](#cluster-delete)
      1. [cluster list-allowed](#cluster-list-allowed)
      1. [cluster allow-group](#cluster-allow-group)
      1. [cluster deny-group](#cluster-deny-group)
      1. [cluster list-group-allowed-apps](#cluster-list-group-allowed-apps)
      1. [cluster allow-group-app](#cluster-allow-group-app)
      1. [cluster deny-group-app](#cluster-deny-group-app)
      1. [cluster ping](#cluster-ping)
   1. [Application Commands](#application-commands)
      1. [app list](#app-list)
      1. [app info](#app-info)
      1. [app get-conf](#app-get-conf)
      1. [app install](#app-install)
   1. [Application Instance Commands](#application-instance-commands)
      1. [instance list](#instance-list)
      1. [instance info](#instance-info)
      1. [instance restart](#instance-restart)
      1. [instance delete](#instance-delete)
      1. [instance logs](#instance-logs)
      1. [instance scale](#instance-scale)
   1. [Secret Commands](#secret-commands)
      1. [secret list](#secret-list)
      1. [secret create](#secret-create)
      1. [secret copy](#secret-copy)
      1. [secret delete](#secret-delete)
      1. [secret info](#secret-info)

Installing
==========
Pre-built binaries are available [for Linux](https://jenkins.slateci.io/artifacts/client/slate-linux.tar.gz) and [for Mac OS](https://jenkins.slateci.io/artifacts/client/slate-macos.tar.gz) (versions >=10.9 are supported). 

Usage
=====

Configuration
-------------
`slate` expects to read your SLATE access token from the file $HOME/.slate/token (which should have permissions set so that it is only readable by you), and the address at which to contact the SLATE API server from $HOME/.slate/endpoint. (Both of these sources of input can be overridden by environment variables and command line options if you so choose.)

General
-------

The SLATE client tool provides a hierarchy of subcommands for actions and categories of actions. Option values can
follow a space or an equal (e.g. `slate --width 80` or `slate --width=80`). Some options have both a short and
a long form (e.g. ``slate -h`` or ``slate --help``).

### --help

A help message can be generated for each command and subcommand.

Examples:

	$ slate --help
	SLATE command line interface
	Usage: slate [OPTIONS] SUBCOMMAND
	
	Options:
	  -h,--help                   Print this help message and exit
	  --no-format                 Do not use ANSI formatting escape sequences in output
	  --width UINT                The maximum width to use when printing tabular output
	  --api-endpoint URL (Env:SLATE_API_ENDPOINT)
	                              The endpoint at which to contact the SLATE API server
	  --api-endpoint-file PATH (Env:SLATE_API_ENDPOINT_PATH)
	                              The path to a file containing the endpoint at which to contact the SLATE API server. 	The contents of this file are overridden by --api-endpoint if that option is specified. Ignored if the specified 	file does not exist.
	  --credential-file PATH (Env:SLATE_CRED_PATH)
	                              The path to a file containing the credentials to be presented to the SLATE API server
	  --output TEXT               The format in which to print output (can be specified as no-headers, json, jsonpointer, jsonpointer-file, custom-columns, or custom-columns-file)
	
	Subcommands:
	  version                     Print version information
	  completion                  Print a shell completion script
	  group                       Manage SLATE groups
	  cluster                     Manage SLATE clusters
	  app                         View and install SLATE applications
	  instance                    Manage SLATE application instances
	  secret                      Manage SLATE secrets

	$ slate app --help
	View and install SLATE applications
	Usage: slate app [OPTIONS] SUBCOMMAND
	
	Options:
	  -h,--help                   Print this help message and exit
	
	Subcommands:
	  list                        List available applications
	  get-conf                    Get the configuration template for an application
	  install                     Install an instance of an application

### --output

The output produced can be given in specified formats rather than the default tabular format, including in JSON format, in tabular format with custom columns, and as a single specified JSON Pointer value.

The supported option values are:
- json - format output as JSON

Example:

	$ slate --output json group list
	[{"apiVersion":"v1alpha3","kind":"Group","metadata":{"id":"group_PTsReW02sI8","name":"slate-dev"}}]


- custom-columns=*column specification* - format output in tabular form according to given column specification

The column specification must be given in the format:

	Header:Attribute,Header:Attribute

Each attribute given must be in the form of a JSON Pointer.

Example:

	$ slate --output custom-columns=Name:/metadata/name,ID:/metadata/id group list
	Name      ID
	slate-dev group_PTsReW02sI8


- custom-columns-file=*file with column specification* - format output in tabular form according to the column specification in given file

File must be formatted with headings in the first line and the corresponding attribute in the form of a JSON Pointer beneath the header included in the file.

Example file:

	Name		ID
	/metadata/name 	/metadata/id


Example (for file columns.txt as the above example file):

	$ slate --output custom-columns-file=columns.txt group list
	Name      ID
	slate-dev group_PTsReW02sI8


- no-headers - format output in default tabular form with headers suppressed

Example:

	$ slate --output no-headers group list
	slate-dev group_PTsReW02sI8


- jsonpointer=*pointer specification* - output the value of given JSON Pointer

Example:

	$ slate --output jsonpointer=/items/0/metadata/name group list
	slate-dev


- jsonpointer-file=*file with pointer specification* - output the value of the JSON Pointer in the given file

Example file:

	/items/0/metadata/id

Example (for file pointer.txt as the above example file):

	$ slate --output jsonpointer=pointer.txt group list
	group_PTsReW02sI8
	
### --no-format

This flag can be used to suppress the use of ANSI terminal codes for styled text in the default output format. Text styling is automatically disabled when `slate` detects that its output is not going to an interactive terminal. 
	
### version

This command simply prints version information and exits. 

### version upgrade

This command summarizes the current version information (exactly the same as [version](#version)), checks for a newer version of `slate`, and optionally installs it if it is found. 

### Completions

The slate client comes bundled with completions for several different shells.  These will allow you to "tab complete" each of the slate commands. Completions are bundled for Bash, Fish and Zsh, and are output to stdout by the `slate completion` subcommand. Where the completions are placed will depend on which shell and operating system you are using.

Here are common setups for the supported shells and operating systems.

#### Bash

You can temporarily test the completions by `source`ing them.

	$ source <(slate completion bash)

Completions for system commands are usually stored in `/etc/bash_completion.d`, but can also be stored in `~/.local/share/bash-completion/completions` for user-specific commands. In order to have completions for slate autoloaded, run:

	$ mkdir -p ~/.local/share/bash-completion/completions
	$ slate completion bash > ~/.local/share/bash-completion/completions/slate

Note you will have to restart your shell for the changes to take affect.

	$ exec bash


#### Bash (macOS/Homebrew)

Homebrew stores bash completion files within the Homebrew directory. With the `bash-completion` brew formula installed, run the command:

	$ mkdir -p $(brew --prefix)/etc/bash_completion.d
	$ slate completion bash > $(brew --prefix)/etc/bash_completion.d/slate.bash-completion

As before, you will have to restart your shell.

#### Fish

Fish completion files are commonly stored in `~/.config/fish/completions`. You can source the completions for temporary use by running the command:

	$ slate completion fish | source

Or to install them persistently run:

	$ mkdir -p ~/.config/fish/completions
	$ slate completion fish > ~/.config/fish/completions/slate.fish

And restart your shell.

#### Zsh

Zsh completions are commonly stored in any directory listed in your `$fpath` variable. To use these completions, you must either add the generated script to one of those directories, or add your own to this list.

Adding a custom directory is often the safest bet. For example, if you wish to use `~/.zfunc`, first run:

	$ mkdir ~/.zfunc

Then add the following lines to your `.zshrc` just before `compinit`:

	fpath+=~/.zfunc

Now you can install the completion script using the following command:

	$ slate completion zsh > ~/.zfunc/_slate

Then restart your shell.

Group Commands
-----------

These commands allow the user to create/list/delete vos on the SLATE platform. Group names and IDs are each, and may be used interchangeably. 

### group list

Lists the currently available groups.

Example:

	$ slate group list
	Name       ID
	slate-dev  group_PTsReW02sI8
	another-group group_NUKQUeNjMMo
	
### group info

Displays more detailed information about a single group. 

Example:

	$ slate group info slate-dev
	Name      Field             Email            Phone        ID            
	slate-dev Resource Provider slate@slateci.io 312-555-5555 group_PTsReW02sI8
	Description: SLATE platform development

### group create

Creates a new group. 

The `--field` option must be used to specify the field of science in which this group does its work. This can be 'Resource Provider' for groups which provide computing resources, rather than doing science research. 

Example:

	$ slate group create my-group --field chemistry
	Successfully created group my-group with ID group_tHllvsT8fEk
	
### group update

Update one or more of the properties of a group with new values. A group's contact email address, phone number, field of science, and description can be updated using this command. 

Example:

	$ slate group update my-group --email biochem@somewhere.edu --phone 773-555-5555 --field biochemistry --desc 'A biochemistry research group'
	Successfully updated group my-group
	$ slate group info my-vo
	Name  Field        Email                 Phone        ID            
	my-group Biochemistry biochem@somewhere.edu 773-555-5555 group_tHllvsT8fEk
	Description: A biochemistry research group

### group delete

Deletes a group.

Example:

	$ slate group delete my-group
	Are you sure you want to delete group group_tHllvsT8fEk (my-group)? y/[n]: y
	Successfully deleted group my-group

### group list allowed clusters

Lists all clusters that can be accessed from a single group (includes clusters that allow any group)

Example

	$ slate group list-allowed-clusters my-group
	Cluster              ID               
	my-cluster           group_XXXXXXXXXXX
	permissible-cluster  *

Cluster Commands
----------------

These commands allow the user to manage the clusters available on the SLATE platform. Cluster names and IDs are each, and may be used interchangeably. 

### cluster list

List the currently available clusters. Optionally limit the list to clusters which a particular group is allowed on, using the `--group` flag.

Example:

	$ slate cluster list
	Name          Admin     ID                                          
	umich-prod    slate-dev cluster_WRb0f8mH9ak
	uchicago-prod slate-dev cluster_yZroQR5mfBk
	utah-bunt     slate-dev cluster_AoP8UISHZqU
	utah-coreos   slate-dev cluster_vnYqjHgT5o0

For a Group called `utah-group` that is only allowed on `utah-bunt` and `utah-coreos`:

	$ slate cluster list --group utah-group 
	Name        Admin     ID                                          
	utah-bunt   slate-dev cluster_AoP8UISHZqU
	utah-coreos slate-dev cluster_vnYqjHgT5o0
	
### cluster info

Displays detailed information about one cluster. 

Example:

	$ slate cluster info my-cluster
	Name       Admin    Owner          ID                 
	my-cluster my-group My Institution cluster_AEcDl9lh8fE

### cluster create

Add a kubernetes cluster to the SLATE platform. 

By default, this command relies on picking up the cluster to add from your curent environment. *Before running this command you should verify that you have the correct cluster selected.* `kubectl config current-context` and `kubectl cluster-info` may be good starting points to ensure that your kubeconfig is what you expect it to be. 

When using this subcommand, a group must be specified. This will be the group which administers the cluster within SLATE, and only members of that group will be able to manipulate (i.e. delete) it and manage access to it. Additionally, the organization which owns the cluster (i.e. purchased its hardware) must be specified. 

Example:

	$ slate cluster create --group my-group --org "My Institution" my-cluster
	...
	Successfully created cluster my-cluster with ID cluster_AEcDl9lh8fE

### cluster update

Update one or more of the properties of a cluster with new values. A cluster's contact owning organization and list of geographical locations can be updated using this command. Additionally, this command can be used (with the `-r` option) to update the Kubernetes configuration used to contact the cluster. This is useful if a cluster has moved to a different IP address, or has been destroyed and recreated but you wish to present it as logically the same within SLATE. 

Example:

	$ slate cluster update my-cluster --org "Other Institution" --location 45.787,-108.537
	Successfully updated cluster my-cluster
	$ slate cluster info my-cluster
	Name       Admin    Owner             ID                 
	my-cluster my-group Other Institution cluster_AEcDl9lh8fE
	
	Latitude Longitude
	45.787   -108.537 

### cluster delete

Remove a cluster from the SLATE platform. 

Only members of the group which owns a cluster may remove it. 

Example:

	$ slate cluster delete my-cluster
	Are you sure you want to delete cluster cluster_yZroQR5mfBk (my-cluster) belonging to group my-group? y/[n]: y
	Successfully deleted cluster my-cluster

### cluster list-allowed

List all groups allowed to run applications on a cluster. 

By default only the group which owns a cluster may run applications on it. Additional groups may be granted access using the `cluster allow-group` command. 

Example:

	$ slate cluster list-allowed my-cluster
	Name      ID
	slate-dev group_PTsReW02sI8

### cluster allow-vo

Grant a group access to use a cluster. 

Only members of the group which owns a cluster can grant access to it. Granting access to the special group pseudo-ID `*` will allow _any_ group (including subsequently created groups) to use the cluster. 

Example:

	$ slate cluster allow-group my-cluster another-vo
	Successfully granted group another-group access to cluster my-cluster
	$ slate cluster list-allowed my-cluster
	Name       ID
	slate-dev  group_PTsReW02sI8
	another-group group_NUKQUeNjMMo

### cluster deny-vo

Revoke a group's access to use a cluster. 

Only members of the group which owns a cluster can revoke access to it. The owning group's access cannot be revoked. Revoking access for the group pseudo-ID `*` removes permission for groups not specifically granted access to use the cluster. 

Example:

	$ slate cluster deny-group my-cluster another-vo
	Successfully revoked Group another-group access to cluster my-cluster
	$ slate cluster list-allowed my-cluster
	Name       ID
	slate-dev  group_PTsReW02sI8

### cluster list-group-allowed-apps

List applications a group is allowed to use on a cluster.

By default, a group which has been granted access to a cluster may install any application there, but the cluster administrators may place restrictions on which applications the group may use. This command allows inspections of which restrictions, if any, are in effect. 

Example:

	$ slate cluster list-group-allowed-apps my-cluster my-vo
	Name
	<all>
	$ slate cluster list-group-allowed-apps my-cluster another-vo
	Name              
	nginx             
	osg-frontier-squid

### cluster allow-group-app

Grant a group permission to use an application on a cluster.

By default, a group which has been granted access to a cluster may install any application there. Granting access to one or more specifically named applications replaces this universal permission with permission to use only the specific applications. Universal permission can be restored by granting permission for the special pseudo-application `*`.

Example:

	$ slate cluster list-group-allowed-apps my-cluster another-vo
	Name
	<all>
	$ ./slate cluster allow-group-app my-cluster another-group osg-frontier-squid
	Successfully granted group another-group permission to use osg-frontier-squid on cluster my-cluster
	$ slate cluster list-group-allowed-apps my-cluster another-vo
	Name              
	osg-frontier-squid
	$ ./slate cluster allow-group-app my-cluster another-group '*'
	Successfully granted Group another-group permission to use * on cluster my-cluster
	$ ./slate cluster list-group-allowed-apps my-cluster another-vo
	Name              
	<all>

### cluster deny-group-app

Remove a group's permission to use an application on a cluster. 

By default, a group which has been granted access to a cluster may install any application there. This universal permission can be removed by denying permission for the special pseudo-application `*`, which also removes any permissions granted for specific applications. Permission can also be revoked for single applications. 

Example:

	$ slate cluster list-group-allowed-apps my-cluster another-vo
	Name
	<all>
	$ ./slate cluster deny-group-app my-cluster another-group '*'
	Successfully removed group another-group permission to use * on cluster my-cluster
	$ slate cluster list-group-allowed-apps my-cluster another-vo
	Name
	$ ./slate cluster allow-group-app my-cluster another-group osg-frontier-squid
	Successfully granted group another-group permission to use osg-frontier-squid on cluster my-cluster
	$ ./slate cluster allow-group-app my-cluster another-group nginx
	Successfully granted group another-group permission to use nginx on cluster my-cluster
	$ ./slate cluster deny-group-app my-cluster another-group nginx
	Successfully removed group another-group permission to use nginx on cluster my-cluster
	$ slate cluster list-group-allowed-apps my-cluster another-vo
	Name
	osg-frontier-squid

### cluster ping

Check whether the platform can connect to a cluster. 

Example:

	$ slate cluster ping cluster1
	Cluster cluster1 is reachable
	$ slate cluster ping cluster2
	Cluster cluster2 is ot reachable

Application Commands
--------------------

These commands allow the user to view available applications and install them on the SLATE platform. 

### app list

List the applications currently available for installation form the catalogue.

Example:

	$ slate app list
	Name               App Version Chart Version Description
	jupyterhub         v0.8.1      v0.7-dev      Multi-user Jupyter installation                   
	osiris-unis        1.0         0.1.0         Unified Network Information Service (UNIS)        
	perfsonar          1.0         0.1.0         perfSONAR is a network measurement toolkit...

### app info

Download the readme file for an application. The resulting data is written to stdout, although it can optionally be directed to a file. 

Example:

	./slate app info osg-frontier-squid
	OSG Frontier Squid for Helm #
	
	----
	## Deployment
	The application is packaged as osg-frontier-squid.
	
	Deployments of this package will be labeled as `osg-frontier-squid-[Tag]`, where Tag is a required field in the values.yaml file.
	
	Customization options are provided in the values.yaml file and can be overwritten by adjusting a copy of this file, and running `helm install osg-frontier-squid --values [myvalues].yaml` where myvalues is the name of your file.
	
	For a comprehensive list of customization options and descriptions, please see the `values.yaml` file.
	
	----
	## Application
	Frontier Squid is an HTTP cache, providing *quick access to recently downloaded data*.
	...

### app get-conf

Download the configuration file for an application for customization. The resulting data is written to stdout, it is useful in most cases to pipe it to a file where it can be edited and then used as an input for the `app install` command. 

Example:
	$ slate app get-conf osg-frontier-squid
	# Instance to label use case of Frontier Squid deployment
	# Generates app name as "osg-frontier-squid-[Instance]"
	# Enables unique instances of Frontier Squid in one namespace
	Instance: global
	
	Service:
	  # Port that the service will utilize.
	  Port: 3128
	  # Controls whether the service is accessible from outside of the cluster.
	  # Must be true/false
	  ExternallyVisible: true
	
	SquidConf:
	  # The amount of memory (in MB) that Frontier Squid may use on the machine.
	  # Per Frontier Squid, do not consume more than 1/8 of system memory with Frontier Squid
	  CacheMem: 128
	  # The amount of disk space (in MB) that Frontier Squid may use on the machine.
	  # The default is 10000 MB (10 GB), but more is advisable if the system supports it.
	  CacheSize: 10000

### app install

Install an instance of an application to one of the clusters in the SLATE platform. 

When using this subcommand, a group and a cluster must be specified. The group will be considered the owner of the resulting application instance (so only members of that group will be able to delete it), and the cluster is where the instance will be installed. 

Details of how the application behaves can be customized by supplying a configuration file (with the `--conf` option), originally obtained using the `app get-conf` command. 

To install more than one instance of the same application on the same cluster, a _tag_ should be specified for at least one of them, by changing the value set for the `Instance` key in the configuration. This is simply a short, descriptive string which is appended to the instance name, both for uniqueness and convenience on the part of the user recognizing which instance is which. 

After the instance is installed, it can be examined and manipulated using the `instance` family of commands. 

Example:

	$ slate app install --group my-group --cluster some-cluster osg-frontier-squid
	Successfully installed application osg-frontier-squid as instance my-group-osg-frontier-squid-test with ID instance_UCqXH5OkMdo

In this case, the osg-frontier-squid application is installed with all configuration left set to defaults. The full instance name is the combination of the group name, the application name, and the user-supplied tag. 

This command also has a second usage: To install a helm chart which is stored locally, rather than one published in the application catalog. This use is not permitted by default, but is enabled in testing environments like [miniSLATE](https://github.com/slateci/minislate) in order to allow application developers to test their charts. This special mode is triggered by using the `--local` option, which changes the interpretation of the specified application name from being a name to be looked up in the catalog to being a local filesystem path to the chart to be uploaded to the server and installed. 

Application Instance Commands
-----------------------------
These commands allow the user to view and manipulate running application instances on the SLATE platform. 

### instance list

Lists the apllication instances which are currently running. At this time, commands which operate on particular instances require the instance ID, not the instance name. 

Example:

	$ slate instance list
	Name                    Group     Cluster      ID
	osg-frontier-squid-test slate-dev some-cluster instance_UCqXH5OkMdo

### instance info

Get detailed information about a particular application instance. 

Example:

	./slate instance info instance_UCqXH5OkMdo
	Name                    Started      Group    Cluster      ID
	osg-frontier-squid-test 2018-Dec-03  my-group some-cluster instance_UCqXH5OkMdo
	                        21:24:54 UTC                     
	
	Services:
	Name                    Cluster IP   External IP     ports         
	osg-frontier-squid-test 10.98.10.193 192.170.227.240 3128:31052/TCP
	
	Pods:
	  osg-frontier-squid-test-5d58c88b5d-rj427
	    Status: Running
	    Created: 2018-12-03T21:24:55Z
	    Host: some-node
	    Host IP: 192.170.227.240
	    Conditions: Initialized at 2018-12-03T21:24:56Z
	                Ready at 2018-12-03T21:24:58Z
	                ContainersReady at 2018-12-03T21:24:58Z
	                PodScheduled at 2018-12-03T21:24:56Z
	    Containers:
	      fluent-bit
	        State: running since 2018-12-03T21:24:57Z
	        Ready: true
	        Restarts: 0
	        Image: fluent/fluent-bit:0.13.4
	      osg-frontier-squid
	        State: running since 2018-12-03T21:24:57Z
	        Ready: true
	        Restarts: 0
	        Image: slateci/osg-frontier-squid:0.1
	
	Configuration: (default)

### instance restart

Stop and restart a running application instance. 

Note that the 'start' time of the entire instance will not be changed, but the instance's pods and containers will all reflect the time at which it is restarted. However, the container restart count fetched from Kubernetes will not be incremented, rather it will be reset to zero, since technically the old containers were destroyed and entirely new ones created. 

Example:

	$ date -u
	Thu Dec 06 20:16:49 UTC 2018
	$ slate instance restart instance_UCqXH5OkMdo
	Successfully restarted instance instance_UCqXH5OkMdo
	./slate instance info instance_UCqXH5OkMdo
	Name                    Started      Group    Cluster      ID
	osg-frontier-squid-test 2018-Dec-03  my-group some-cluster instance_UCqXH5OkMdo
	                        21:24:54 UTC                     
	
	Services:
	Name                    Cluster IP   External IP     ports         
	osg-frontier-squid-test 10.98.10.193 192.170.227.240 3128:31052/TCP
	
	Pods:
	  osg-frontier-squid-test-5d58c88b5d-rj427
	    Status: Running
	    Created: 2018-12-06T20:16:55Z
	    Host: some-node
	    Host IP: 192.170.227.240
	    Conditions: Initialized at 2018-12-06T20:16:56Z
	                Ready at 2018-12-06T20:16:58Z
	                ContainersReady at 2018-12-06T20:16:58Z
	                PodScheduled at 2018-12-06T20:16:56Z
	    Containers:
	      fluent-bit
	        State: running since 2018-12-06T20:16:57Z
	        Ready: true
	        Restarts: 0
	        Image: fluent/fluent-bit:0.13.4
	      osg-frontier-squid
	        State: running since 2018-12-06T20:16:57Z
	        Ready: true
	        Restarts: 0
	        Image: slateci/osg-frontier-squid:0.1
	
	Configuration: (default)

### instance delete

Delete an application instance. This operation is permanent, and the system will forget all configuration information for the instance once it is deleted. If you need this information, you should make sure you have it recorded before running this command (the `instance info` command may be useful here). 

Example:

	$ slate instance delete instance_UCqXH5OkMdo
	Are you sure you want to delete instance instance_UCqXH5OkMdo (osg-frontier-squid-test) belonging to group my-group from cluster some-cluster? y/[n]: y
	Successfully deleted instance instance_UCqXH5OkMdo
	
### instance logs

Get the logs (standard output) from the pods in an instance. By default logs are fetched for all containers belonging to all pods which are part of the instance, and the 20 most recent lines of output are fetched from each log. The `--container` option can be used to request the log form just a particular container, and the `--max-lines` option can be used to change how much of the log is fetched. 

Example:

	$ slate instance logs instance_UCqXH5OkMdo
	========================================
	Pod: osg-frontier-squid-test-5f6c578fcc-hlwrc Container: osg-frontier-squid
	2018/12/03 21:24:59| HTCP Disabled.
	2018/12/03 21:24:59| Squid plugin modules loaded: 0
	2018/12/03 21:24:59| Adaptation support is off.
	2018/12/03 21:24:59| Accepting HTTP Socket connections at local=[::]:3128 remote=[::] FD 17 flags=9
	2018/12/03 21:24:59| Done scanning /var/cache/squid dir (0 entries)
	========================================
	Pod: osg-frontier-squid-test-5f6c578fcc-hlwrc Container: fluent-bit
	[2018/12/03 21:24:57] [ info] [engine] started (pid=1)
	[2018/12/03 21:24:57] [ info] [http_server] listen iface=0.0.0.0 tcp_port=2020
	
Here, the instance has one pod with two containers, but neither has yet written anything to its log. 

### instance scale

This command can be used to both query the current number of replicas an application instance has and to change the number of replicas requested. The `--replicas` option is used to specify a new target number of replicas; if it is omitted no change is made and the current number of replicas is returned. The `--deployment` option can be used to select a deployment to scale if the application contains more than one, or to filter the output give by the query mode. 

Example:

	$ slate instance scale instance_UCqXH5OkMdo
	Deployment              Replicas
	osg-frontier-squid-test 1
	$ slate instance scale instance_UCqXH5OkMdo --replicas 3
	Successfully scaled instance_UCqXH5OkMdo to 3 replicas.
	Deployment              Replicas
	osg-frontier-squid-test 3       

Secret Commands
---------------
These commands allow managing sensitive data as kubernetes secrets. This is the recommanded method for making data such as passwords and certificates available to application instances. Secrets are only accessible to members of the group which owns them. See [the Kubernetes documentation](https://kubernetes.io/docs/concepts/configuration/secret/#use-cases) for more details of how pods can use secrets. Secrets installed through SLATE are also persisted in the SLATE central storage in encrypted form. 

### secret list

List the secrets installed for a particular Group, optionally limiting scope to a particular cluster.

Example:

	$ slate secret list --group my-vo
	Name     Created                  Group    Cluster  ID                                         
	mysecret 2018-Aug-09 20:19:51 UTC my-group cluster1 secret_1OaTkAMfpdM
	a-secret 2018-Aug-15 17:12:56 UTC my-group cluster2 secret_7sIv5NR1fhk
	$ slate secret list --group my-group --cluster cluster2
	Name     Created                  Group    Cluster  ID                                         
	a-secret 2018-Aug-15 17:12:56 UTC my-group cluster2 secret_7sIv5NR1fhk

### secret create

Install a secret on a cluster. The owning group for the secret must be specified as well as the cluster on which it will be placed. Because secrets are namespaced per-group and per-cluster names may be reused; within one group the same secret name may be used on many clusters, and secret names chosen by other groups do not matter. Secrets are structured as key, value mappings, so several pieces of data can be stored in a single secret if they are all intended to be used together. Any number of key, value pairs may be specified, however, the total data size (including keys, values, and the metadata added by SLATE) is limited to 400 KB. 

Keys and values may be specified one pair at a time using `--from-literal key=value`. Value data can also be read from a file using `--from-file`, where the file's name is taken as the key. By default the file's base name (omitting the enclosing directory path), but this can be overridden: `--from-file key=/actual/file/path`. This is particularly useful if the file's original name contains charcters not permitted by kubernetes in secret keys (the allowed characters are [a-zA-Z0-9._-]). If the argument to `--from-file` is a directory, that directory will be scanned and each file it contains whose name meets the kubernetes key requirements will be read and added as a value. Finally, key, value pairs may be read in from a file with lines structured as `key=value` using the `--from-env-file` option. Any number and any combination of these options may be used to input all desired data. If the same key is specified more than once the result is not defined; it is recommended that this should be avoided. 

Example:

	$ slate secret create --group mv-group --cluster cluster1 important-words --from-literal=foo=bar --from-literal=baz=quux
	Successfully created secret important-words with ID secret_Ae7-Nndg-yw
	
### secret copy

Copy an existing secret to a new name or a different cluster. The source secret to be copied from must be specified by its ID, and the new secret's name follows the same rules as for direct creation. As with creating a secret directly, the Group which will own the new secret and the cluster on which the secret will be placed must be specified. 

Examples:

	$ slate secret copy secret_Ae7-Nndg-yw copied-secret --cluster cluster2 --group mv-vo
	Successfully created secret copied-secret with ID secret_t23HkWWkxmg

### secret delete

Remove a previously installed secret. Only members of the group which owns the secret may delete it. 

Example:

	$ slate secret delete secret_t23HkWWkxmg
	Are you sure you want to delete instance secret_t23HkWWkxmg (osg-frontier-squid-test) belonging to group my-group from cluster cluster2? y/[n]: y
	Successfully deleted secret secret_t23HkWWkxmg

### secret info

Fetch the contents of a secret and its metadata. Only members of the group which owns the secret may view it.  

Example:

	$ slate secret info secret_Ae7-Nndg-yw
	Name            Created                  Group    Cluster  ID                                         
	important-words 2018-Aug-15 20:41:09 UTC my-group cluster1 secret_Ae7-Nndg-yw
	
	Contents:
	Key Value
	foo bar  
	baz quux 
