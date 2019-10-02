Summary
=======
This project contains the source code for both the client and server components of the SLATE platform. These components can be built independently, following the instructions below. 

For information on using the SLATE client see [the client manual](resources/docs/client_manual.md). 

Dependencies
============

Common:

- [CMake](https://cmake.org)
- [OpenSSL](https://www.openssl.org)
- [libcurl](https://curl.haxx.se/libcurl/)
- [zlib](https://www.zlib.net)

Server Only:

- [Boost](https://www.boost.org)
- [Amazon AWS C++ SDK](https://github.com/aws/aws-sdk-cpp)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)

Additionally, [kubectl](https://kubernetes.io/docs/tasks/tools/install-kubectl/) is needed at runtime by the server and by the the client under some circumstances, and the server requires [helm](https://helm.sh)

This project also uses [crow](https://github.com/ipkn/crow), [RapidJSON](http://rapidjson.org), [libcuckoo](https://github.com/efficient/libcuckoo), and [scrypt](https://www.tarsnap.com/scrypt.html), but each of these dependencies is sufficiently lightweight that it is copied directly into this codebase. 

Installing dependencies on a fresh CentOS 7 system
--------------------------------------------------
Note that the CentOS 7 Cmake package is too old to build the AWS SDK, so it is necessary to use the `cmake3` package from EPEL. This also means that all `cmake` commands must be replaced with `cmake3` on CentOS systems. 

	sudo yum install -y gcc-c++
	sudo yum install -y openssl-devel
	sudo yum install -y libcurl-devel
	sudo yum install -y zlib-devel
	sudo yum install -y epel-release
	sudo yum install -y cmake3
	
	cat <<EOF > kubernetes.repo
	[kubernetes]
	name=Kubernetes
	baseurl=https://packages.cloud.google.com/yum/repos/kubernetes-el7-x86_64
	enabled=1
	gpgcheck=1
	repo_gpgcheck=1
	gpgkey=https://packages.cloud.google.com/yum/doc/yum-key.gpg https://packages.cloud.google.com/yum/doc/rpm-package-key.gpg
	EOF
	sudo mv kubernetes.repo /etc/yum.repos.d/kubernetes.repo
	sudo yum install -y kubectl
	
Building the server additionally requires:

	sudo yum install -y boost-devel
	sudo yum install -y yaml-cpp-devel

Installing dependencies on Ubuntu
---------------------------------
	sudo apt-get install g++  
	sudo apt-get install libssl-dev
	sudo apt-get install libcurl4-openssl-dev
	sudo apt-get install libz-dev
	sudo apt-get install cmake
	sudo apt-get install kubectl
	
Building the server additionally requires:

	sudo apt-get install libboost-all-dev
	sudo apt-get install libyaml-cpp-dev
	
Installing dependencies on FreeBSD
----------------------------------
(This section is currently incomplete)

	sudo pkg install curl
	sudo pkg install cmake

Installing the AWS C++ SDK
--------------------------
RPMs do not appear to be avilable for this library, so it must be built from source. 

In a suitable location:

	curl -LO https://github.com/aws/aws-sdk-cpp/archive/1.7.25.tar.gz
	tar xzf 1.7.25.tar.gz
	mkdir aws-sdk-cpp-1.7.25-build
	cd aws-sdk-cpp-1.7.25-build
	cmake ../aws-sdk-cpp-1.7.25 -DBUILD_ONLY="dynamodb;route53" -DBUILD_SHARED_LIBS=Off
	make
	sudo make install

Building:
=========
In the slate-client-server directory:

	mkdir build
	cd build
	cmake .. [options]
	make
	
Options for cmake include:

- `-DBUILD_CLIENT=<True|False>` which sets whether the client will be built (default is `True`)
- `-DBUILD_SERVER=<True|False>` which sets whether the server will be built (default is `True`)
- `-DBUILD_SERVER_TESTS=<True|False>` which sets whether the server will be built (default is `True`); this option makes sense only when the server will be built
- `-DSTATIC_CLIENT=True` which builds the client as a static binary (defaults to false); this option works correctly only on Alpine Linux (or a system with suitable static libraries available)

Running `make` will generate the `slate-client` or `slate-service` executables, depending on the options selected. 
