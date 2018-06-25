Dependencies
============
- CMake
- Boost
- OpenSSL
- libcurl
- Amazon AWS C++ SDK
- kubectl

Installing dependencies on a fresh CentOS 7 system
--------------------------------------------------
Note that the CentOS 7 Cmake package is too old to build the AWS SDK, so it is necessary to use the `cmake3` package from EPEL. This also means that all `cmake` commands must be replaced with `cmake3`. 

	sudo yum install -y gcc-c++.x86_64
	sudo yum install -y boost-devel.x86_64
	sudo yum install -y zlib-devel
	sudo yum install -y openssl-devel
	sudo yum install -y libcurl-devel
	sudo yum install -y subversion.x86_64
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

Installing dependencies on Ubuntu
---------------------------------
	sudo apt-get install g++ 
	sudo apt-get install libboost-all-dev 
	sudo apt-get install zlib1g-dev
	sudo apt-get install libssl-dev
	sudo apt-get install libcurl4-openssl-dev
	sudo apt-get install subversion
	sudo apt-get install cmake
	sudo apt-get install kubectl

Installing the AWS C++ SDK
--------------------------
RPMs do not appear to be avilable for this library, so it must be built from source. 

In a suitable location:

	curl -LO https://github.com/aws/aws-sdk-cpp/archive/1.4.70.tar.gz
	tar xzf 1.4.70.tar.gz
	mkdir aws-sdk-cpp-1.4.70-build
	cd aws-sdk-cpp-1.4.70-build
	cmake ../aws-sdk-cpp-1.4.70 -DBUILD_ONLY="dynamodb" -DBUILD_SHARED_LIBS=Off
	make
	sudo make install


Building:
=========
In the slate-api-server directory:

	mkdir build
	cd build
	cmake ..
	make

This should create the `slate-service` executable
