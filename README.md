# SLATE Client and Server

This project contains the source code for both the client and server components of the SLATE platform. These components can be built independently, following the instructions below.
* For information on using the SLATE client see [the client manual](resources/docs/client_manual.md).
* A basic specification for the client-server API can be found [in the resources directory](resources/api_specification).
* For information on using Docker for local development and testing see [Local Development with Docker Compose](resources/docs/docker_compose.md).

## Dependencies

### Common

- [CMake](https://cmake.org)
- [OpenSSL](https://www.openssl.org)
- [libcurl](https://curl.haxx.se/libcurl/)
- [zlib](https://www.zlib.net)

### Server-only

- [Boost](https://www.boost.org)
- [Amazon AWS C++ SDK](https://github.com/aws/aws-sdk-cpp) (see [below](#installing-the-aws-c-sdk) for instructions on building and installing from source)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)

Additionally, [kubectl](https://kubernetes.io/docs/tasks/tools/install-kubectl/) is needed at runtime by the server and by the client under some circumstances, and the server requires [helm](https://helm.sh)

This project also uses [crow](https://github.com/ipkn/crow), [RapidJSON](http://rapidjson.org), [libcuckoo](https://github.com/efficient/libcuckoo), and [scrypt](https://www.tarsnap.com/scrypt.html), but each of these dependencies is sufficiently lightweight that it is copied directly into this codebase. 

## Installing Dependencies

### Centos7

Note that the CentOS 7 Cmake package is too old to build the AWS SDK, so it is necessary to use the `cmake3` package from EPEL. This also means that all `cmake` commands must be replaced with `cmake3` on CentOS systems. 

```shell
yum install -y gcc-c++ openssl-devel libcurl-devel zlib-devel epel-release cmake3
```

```shell
cat <<EOF > /etc/yum.repos.d/kubernetes.repo
[kubernetes]
name=Kubernetes
baseurl=https://packages.cloud.google.com/yum/repos/kubernetes-el7-x86_64
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=https://packages.cloud.google.com/yum/doc/yum-key.gpg https://packages.cloud.google.com/yum/doc/rpm-package-key.gpg
EOF && \
yum install -y kubectl
```
	
Building the server additionally requires:

```shell
yum install -y boost-devel yaml-cpp-devel
```

### Ubuntu

```shell
apt-get install g++ libssl-dev libcurl4-openssl-dev libz-dev cmake kubectl
```
	
Building the server additionally requires:

```shell
apt-get install libboost-all-dev libyaml-cpp-dev
```
	
### FreeBSD

> **_NOTE:_** This section is currently incomplete.

```shell
pkg install curl cmake
```

### Installing the AWS C++ SDK

RPMs do not appear to be available for this library, so it must be built from source. In a suitable location:

```shell
curl -LO https://github.com/aws/aws-sdk-cpp/archive/1.7.345.tar.gz && \
tar xzf 1.7.345.tar.gz && \
mkdir aws-sdk-cpp-1.7.345-build && \
cd aws-sdk-cpp-1.7.345-build && \
cmake ../aws-sdk-cpp-1.7.345 -DBUILD_ONLY="dynamodb;route53" -DBUILD_SHARED_LIBS=Off && \
make && \
make install
```

## Building

Options for `cmake` include:
* `-DBUILD_CLIENT=<True|False>`: whether the client will be built (default is `True`)
* `-DBUILD_SERVER=<True|False>`: whether the server will be built (default is `True`)
* `-DBUILD_SERVER_TESTS=<True|False>`: whether the server test binaries will be built (default is `True`); this option makes sense only when the server will be built.
* `-DSTATIC_CLIENT=True` which builds the client as a static binary (defaults to `False`); this option works correctly only on Alpine Linux (or a system with suitable static libraries available).

Running `make` will generate the `slate-client` or `slate-service` executables, depending on the options selected.

In this project's directory:

```shell
mkdir build && \
cd build && \
cmake .. [options] && \
make
```