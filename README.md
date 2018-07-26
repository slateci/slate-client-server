## Table of Contents

1. [Dependencies](#dependencies)
   1. [Installing dependencies on CentOS 7](#installing-dependencies-on-centos-7)
   1. [Installing dependencies on Ubuntu](#installing-dependencies-on-ubuntu)
1. [Building](#building)
1. [Usage](#usage)
   1. [Configuration](#configuration)
   1. [General](#general)
      1. [--help](#--help)
   1. [VO Commands](#vo-commands)
      1. [vo list](#vo-list)
      1. [vo create](#vo-create)
      1. [vo delete](#vo-delete)
   1. [Cluster Commands](#cluster-commands)
      1. [cluster list](#cluster-list)
      1. [cluster create](#cluster-create)
      1. [cluster dalete](#cluster-delete)
   1. [Application Commands](#application-commands)
      1. [app list](#app-list)
      1. [app get-conf](#app-get-conf)
      1. [app install](#app-install)
   1. [Application Instance Commands](#application-instance-commands)
      1. [instance list](#instance-list)
      1. [instance info](#instance-info)
      1. [instance delete](#instance delete)

Dependencies
============
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
`slate-client` expects to read your SLATE access token from the file $HOME/.slate/token (which should have permissions set so that it is only readable by you), and the address at which to contact the SLATE API server from $HOME/.slate/endpoint. (Both of these sources of inpit can be overridden by environemtn variables and command line options if you so choose.)

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
	Usage: ./slate-client [OPTIONS] SUBCOMMAND
	
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
	
	Subcommands:
	  vo                          Manage SLATE VOs
	  cluster                     Manage SLATE clusters
	  app                         View and install SLATE applications
	  instance                    Manage SLATE application instances

	$ slate-client app --help
	View and install SLATE applications
	Usage: ./slate-client app [OPTIONS] SUBCOMMAND
	
	Options:
	  -h,--help                   Print this help message and exit
	
	Subcommands:
	  list                        List available applications
	  get-conf                    Get the configuration template for an application
	  install                     Install an instance of an application

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
	$ ./slate-client app get-conf osg-frontier-squid
	apiVersion: v1
	appVersion: squid-3
	description: A Helm chart for configuration and deployment of the Open Science Grid's
	  Frontier Squid application.
	name: osg-frontier-squid
	version: 0.2.0
	
	---
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

To install more than one instance of the same application on the same cluster, a _tag_ should be specified for at least one of them. This is simply a short, descriptive string which is appended to the instance name, both for uniqueness and convenience on the part of the user recognizing which instance is which. 

After the instance is installed, it can be examined and manipulated using the `instance` family of commands. 

Example:

	$ slate-client app install --vo my-vo --cluster someones-cluster 
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
