Table of Contents
=================

1. [Installing](#installing)
1. [Dependencies](#dependencies)
   1. [Installing dependencies on CentOS 7](#installing-dependencies-on-centos-7)
   1. [Installing dependencies on Ubuntu](#installing-dependencies-on-ubuntu)
1. [Building](#building)
1. [Usage](#usage)
   1. [Configuration](#configuration)
   1. [General](#general)
      1. [--help](#--help)
      1. [--output](#--output)
      1. [--no-format](#--no-format)
   1. [VO Commands](#vo-commands)
      1. [vo list](#vo-list)
      1. [vo create](#vo-create)
      1. [vo delete](#vo-delete)
   1. [Cluster Commands](#cluster-commands)
      1. [cluster list](#cluster-list)
      1. [cluster create](#cluster-create)
      1. [cluster delete](#cluster-delete)
      1. [cluster list-allowed](#cluster-list-allowed)
      1. [cluster allow-vo](#cluster-allow-vo)
      1. [cluster deny-vo](#cluster-deny-vo)
   1. [Application Commands](#application-commands)
      1. [app list](#app-list)
      1. [app get-conf](#app-get-conf)
      1. [app install](#app-install)
   1. [Application Instance Commands](#application-instance-commands)
      1. [instance list](#instance-list)
      1. [instance info](#instance-info)
      1. [instance delete](#instance-delete)
   1. [Secret Commands](#secret-commands)
      1. [secret list](#secret-list)
      1. [secret create](#secret-create)
      1. [secret delete](#secret-delete)
      1. [secret info](#secret-info)

Installing
==========
Pre-built binaries are available [for Linux](http://jenkins.slateci.io/artifacts/slate-client-linux.tar.gz) and [for Mac OS](http://jenkins.slateci.io/artifacts/slate-client-macos.tar.gz) (versions >=10.9 are supported). 

Dependencies
============
The following dependencies are required in order to build this application from its source code:
- gcc (>=4.8.5)
- CMake (>=3.0.0)
- OpenSSL
- libcurl

Installing dependencies on CentOS 7
-----------------------------------
Note that the CentOS 7 CMake package is too old, so it is necessary to use the `cmake3` package from EPEL. This also means that all `cmake` commands must be replaced with `cmake3`. 

	sudo yum install -y gcc-c++.x86_64
	sudo yum install -y openssl-devel
	sudo yum install -y libcurl-devel
	sudo yum install -y epel-release
	sudo yum install -y cmake3

Installing dependencies on Ubuntu
---------------------------------
	sudo apt-get install g++
	sudo apt-get install libssl-dev
	sudo apt-get install libcurl4-openssl-dev
	sudo apt-get install cmake

Building
========
In the slate-remote-client directory:

	mkdir build
	cd build
	cmake ..
	make

This should create the `slate-client` executable. 

Usage
=====

Configuration
-------------
`slate-client` expects to read your SLATE access token from the file $HOME/.slate/token (which should have permissions set so that it is only readable by you), and the address at which to contact the SLATE API server from $HOME/.slate/endpoint. (Both of these sources of input can be overridden by environment variables and command line options if you so choose.)

General
-------

The SLATE client tool provides a heirarchy of subcommands for actions and categories of actions. Option values can
follow a space or an equal (e.g. `slate --width 80` or `slate --width=80`). Some options have both a short and
a long form (e.g. ``slate -h`` or ``slate --help``).

### --help

A help message can be generated for each command and subcommand.

Examples:

	$ slate-client --help
	SLATE command line interface
	Usage: slate-client [OPTIONS] SUBCOMMAND
	
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
	  vo                          Manage SLATE VOs
	  cluster                     Manage SLATE clusters
	  app                         View and install SLATE applications
	  instance                    Manage SLATE application instances

	$ slate-client app --help
	View and install SLATE applications
	Usage: slate-client app [OPTIONS] SUBCOMMAND
	
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

	$ slate-client --output json vo list
	[{"apiVersion":"v1alpha1","kind":"VO","metadata":{"id":"VO_741ad8c5-7811-4ffb-9631-c8662a4a13de","name":"slate-dev"}}]


- custom-columns=*column specification* - format output in tabular form according to given column specification

The column specification must be given in the format:

	Header:Attribute,Header:Attribute

Each attribute given must be in the form of a JSON Pointer.

Example:

	$ slate-client --output custom-columns=Name:/metadata/name,ID:/metadata/id vo list
	Name      ID
	slate-dev VO_741ad8c5-7811-4ffb-9631-c8662a4a13de


- custom-columns-file=*file with column specification* - format output in tabular form according to the column specification in given file

File must be formatted with headings in the first line and the corresponding attribute in the form of a JSON Pointer beneath the header included in the file.

Example file:

	Name		ID
	/metadata/name 	/metadata/id


Example (for file columns.txt as the above example file):

	$ slate-client --output custom-columns-file=columns.txt vo list
	Name      ID
	slate-dev VO_741ad8c5-7811-4ffb-9631-c8662a4a13de


- no-headers - format output in default tabular form with headers suppressed

Example:

	$ slate-client --output no-headers vo list
	slate-dev VO_741ad8c5-7811-4ffb-9631-c8662a4a13de


- jsonpointer=*pointer specification* - output the value of given JSON Pointer

Example:

	$ slate-client --output jsonpointer=/items/0/metadata/name vo list
	slate-dev


- jsonpointer-file=*file with pointer specification* - output the value of the JSON Pointer in the given file

Example file:

	/items/0/metadata/id

Example (for file pointer.txt as the above example file):

	$ slate-client --output jsonpointer=pointer.txt vo list
	VO_741ad8c5-7811-4ffb-9631-c8662a4a13de
	
### --no-format

This flag can be used to suppress the use of ANSI terminal codes for styled text in the default output format. Text styling is automatically disabled when `slate-client` detects that its output is not going to an interactive terminal. 


VO Commands
-----------

These commands allow the user to create/list/delete vos on the SLATE platform. VO names and IDs are each, and may be used interchangeably. 

### vo list

Lists the currently available VOs.

Example:

	$ slate-client vo list
	Name      ID
	slate-dev VO_741ad8c5-7811-4ffb-9631-c8662a4a13de

### vo create

Creates a new VO.

Example:

	$ slate-client vo create my-vo
	Successfully created VO my-vo with ID VO_5a7bcf20-805a-4ecc-8e68-84003fa85117

### vo delete

Deletes a VO.

Example:

	$ slate-client vo delete my-vo
	Successfully deleted VO my-vo

Cluster Commands
----------------

These commands allow the user to manage the clusters available on the SLATE platform. Cluster names and IDs are each, and may be used interchangeably. 

### cluster list

List the currently available clusters.

Example:

	$ slate-client cluster list
	Name        ID                                          
	umich       Cluster_3f1d501a-b202-42e3-8064-52768be8a2de
	uchicago    Cluster_0aecf125-df3c-4e2a-8dc3-e35ab9656433
	utah-bunt   Cluster_f189c1f2-e12d-4d98-b9dd-bc8f5daa8fb9
	utah-coreos Cluster_5cebcd2d-b81c-4235-8868-08b99b053bbc

### cluster create

Add a kubernetes cluster to the SLATE platform. 

By default, this command relies on picking up the cluster to add from your curent environment. *Before running this command you should verify that you have the correct cluster selected.* `kubectl config current-context` and `kubectl cluster-info` may be good starting points to ensure that your kubectl is what you expect it to be. 

When using this subcommand, a VO must be specified. This will be the VO which is considered to 'own' the cluster, and only members of that VO will be able to manipulate (i.e. delete) it. 

Example:

	$ slate-client cluster create --vo my-vo my-cluster
	Successfully created cluster my-cluster with ID Cluster_a227d1f2-e364-4d98-59dc-bc8f5daa7b18

### cluster delete

Remove a cluster from the SLATE platform. 

Only members of the VO which owns a cluster may remove it. 

Example:

	$ slate-client cluster delete my-cluster
	Successfully deleted cluster my-cluster

### cluster list-allowed

List all VOs allowed to run applications on a cluster. 

By default only the VO which owns a cluster may run applications on it. Additional VOs may be granted access using the `cluster allow-vo` command. 

Example:

	$ slate-client cluster list-allowed my-cluster
	Name      ID
	slate-dev VO_741ad8c5-7811-4ffb-9631-c8662a4a13de

### cluster allow-vo

Grant a VO access to use a cluster. 

Only members of the VO which owns a cluster can grant access to it. 

Example:

	$ slate-client cluster allow-vo my-cluster another-vo
	Successfully granted VO another-vo access to cluster my-cluster
	$ slate-client cluster list-allowed my-cluster
	Name       ID
	slate-dev  VO_741ad8c5-7811-4ffb-9631-c8662a4a13de
	another-vo VO_487634b1-ef92-8732-b235-4e756a9835f2

### cluster deny-vo

Revoke a VO's access to use a cluster. 

Only members of the VO which owns a cluster can revoke access to it. The owning VO's access cannot be revoked. 

Example:

	$ slate-client cluster deny-vo my-cluster another-vo
	Successfully revoked VO another-vo access to cluster my-cluster
	$ slate-client cluster list-allowed my-cluster
	Name       ID
	slate-dev  VO_741ad8c5-7811-4ffb-9631-c8662a4a13de

Application Commands
--------------------

These commands allow the user to view available applications and install them on the SLATE platform. 

### app list

List the applications currently available for installation form the catalogue.

Example:

	$ slate-client app list
	Name               App Version Chart Version Description
	jupyterhub         v0.8.1      v0.7-dev      Multi-user Jupyter installation                   
	osiris-unis        1.0         0.1.0         Unified Network Information Service (UNIS)        
	perfsonar          1.0         0.1.0         perfSONAR is a network measurement toolkit...

### app get-conf

Download the configuration file for an application for customization. The resulting data is written to stdout, it is useful in most cases to pipe it to a file where it can be edited and then used as an input for the `app install` command. 

Example:
	$ slate-client app get-conf osg-frontier-squid
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

When using this subcommand, a VO and a cluster must be specified. The VO will be considered the owner of the resulting application instance (so only members of that VO will be able to delete it), and the cluster is where the instance will be installed. 

Details of how the application behaves can be customized by supplying a configuration file (with the `--conf` option), originally obtained using the `app get-conf` command. 

To install more than one instance of the same application on the same cluster, a _tag_ should be specified for at least one of them, by changing the value set for the `Instance` key in the configuration. This is simply a short, descriptive string which is appended to the instance name, both for uniqueness and convenience on the part of the user recognizing which instance is which. 

After the instance is installed, it can be examined and manipulated using the `instance` family of commands. 

Example:

	$ slate-client app install --vo my-vo --cluster someones-cluster osg-frontier-squid
	Successfully installed application osg-frontier-squid as instance my-vo-osg-frontier-squid-test with ID Instance_264f6d11-ed54-4244-a7b0-666fe0a87f2d

In this case, the osg-frontier-squid application is installed with a tag of 'test' and all configuration left set to defaults. The full instance name is the combination of the VO name, the application name, and the user-supplied tag. 

Application Instance Commands
-----------------------------
These commands allow the user to view and manipulate running application instances on the SLATE platform. 

### instance list

Lists the apllication instances which are currently running. At this time, commands which operate on particular instances require the instance ID, not the instance name. 

Example:

	$ slate-client instance list
	Name                              Started               VO    Cluster   ID
	slate-dev-osg-frontier-squid-test 2018-Jul-26 17:42:42  my-vo someones- Instance_264f6d11-ed54-4244-a7b0-
	                                  UTC                         cluster   666fe0a87f2d

### instance info

Get detailed information about a particular application instance. 

Example:

	$ slate-client instance info Instance_264f6d11-ed54-4244-a7b0-666fe0a87f2d
	Name                              Started               VO    Cluster   ID
	slate-dev-osg-frontier-squid-test 2018-Jul-26 17:42:42  my-vo someones- Instance_264f6d11-ed54-4244-a7b0-
	                                  UTC                         cluster   666fe0a87f2d

	Services:
	Name                      Cluster IP   External IP     ports         
	osg-frontier-squid-global 10.98.10.193 192.170.227.240 3128:31052/TCP
	
	Configuration: (default)

### instance delete

Delete an application instance. This operation is permanent, and the system will forget all configuration information for the instance once it is deleted. If you need this information, you should make sure you have it recorded before running this command (the `instance info` command may be useful here). 

Example:

	$ slate-client instance delete Instance_264f6d11-ed54-4244-a7b0-666fe0a87f2d
	Successfully deleted instance Instance_264f6d11-ed54-4244-a7b0-666fe0a87f2d

Secret Commands
---------------
These commands allow managing sensitive data as kubernetes secrets. This is the recommanded method for making data such as passwords and certificates available to application instances. Secrets are only accessible to members of the VO which owns them. See [the Kubernetes documentation](https://kubernetes.io/docs/concepts/configuration/secret/#use-cases) for more details of how pods can use secrets. Secrets installed through SLATE are also persisted in the SLATE central storage in encrypted form. 

### secret list

List the secrets installed for a particular VO, optionally limiting scope to a particular cluster.

Example:

	$ slate-client secret list --vo my-vo
	Name     Created                  VO    Cluster  ID                                         
	mysecret 2018-Aug-09 20:19:51 UTC my-vo cluster1 Secret_15e52946-2869-4f80-838b-b433c86f5ac6
	a-secret 2018-Aug-15 17:12:56 UTC my-vo cluster2 Secret_c185f4f2-3a47-42a3-9001-5a64f5d259c9
	$ slate-client secret list --vo my-vo --cluster cluster2
	Name     Created                  VO    Cluster  ID                                         
	a-secret 2018-Aug-15 17:12:56 UTC my-vo cluster2 Secret_c185f4f2-3a47-42a3-9001-5a64f5d259c9

### secret create

Install a secret on a cluster. The owning VO for the secret must be specified as well as the cluster on which it will be placed. Because secrets are namespaced per-VO and per-cluster names may be reused; within one VO the same secret name may be used on many clusters, and secret names chosen by other VOs do not matter. Secrets are structured as key, value mappings, so several pieces of data can be stored in a single secret if they are all intended to be used together. Any number of key, value pairs may be specified, however, the total data size (including keys, values, and the metadata added by SLATE) is limited to 400 KB. 

Keys and values may be specified one pair at a time using `--from-literal key=value`. Value data can also be read from a file using `--from-file`, where the file's name is taken as the key. By default the file's base name (omitting the enclosing directory path), but this can be overridden: `--from-file key=/actual/file/path`. This is particularly useful if the file's original name contains charcters not permitted by kubernetes in secret keys (the allowed characters are [a-zA-Z0-9._-]). If the argument to `--from-file` is a directory, that directory will be scanned and each file it contains whose name meets the kubernetes key requirements will be read and added as a value. Finally, key, value pairs may be read in from a file with lines structured as `key=value` using the `--from-env-file` option. Any number and any combination of these options may be used to input all desired data. If the same key is specified more than once the result is not defined; it is recommended that this should be avoided. 

Example:

	$ slate-client secret create --vo mv-vo --cluster cluster1 important-words --from-literal=foo=bar --from-literal=baz=quux
	Successfully created secret important-words with ID Secret_bf1ba11e-a389-4e91-97b2-736811bdb829

### secret delete

Remove a previously installed secret. Only members of the VO which owns the secret may delete it. 

Example:

	$ slate-client secret delete Secret_bf1ba11e-a389-4e91-97b2-736811bdb829
	Successfully deleted secret Secret_bf1ba11e-a389-4e91-97b2-736811bdb829

### secret info

Fetch the contents of a secret and its metadata. Only members of the VO which owns the secret may view it.  

Example:

	$ slate-client secret info Secret_bf1ba11e-a389-4e91-97b2-736811bdb829
	Name            Created                  VO    Cluster  ID                                         
	important-words 2018-Aug-15 20:41:09 UTC my-vo cluster1 Secret_bf1ba11e-a389-4e91-97b2-736811bdb829
	
	Contents:
	Key Value
	foo bar  
	baz quux 
