# CLion remote docker environment (How to build docker container, run and stop it)
#
# Build and run:
#   docker build -t clion/centos7-cpp-env:0.1 -f Dockerfile.centos7-cpp-env .
#   docker run -d --cap-add sys_ptrace -p127.0.0.1:2222:22 clion/centos7-cpp-env:0.1
#   ssh-keygen -f "$HOME/.ssh/known_hosts" -R "[localhost]:2222"
#
# stop:
#   docker stop clion_remote_env
#
# ssh credentials (test user):
#   user@password


#FROM centos:7
# FROM hub.opensciencegrid.org/slate/slate-client-server:1.0.7
ARG baseimage=hub.opensciencegrid.org/slate/slate-client-server:2.0.4
#ARG baseimage=localhost/slate-rocky:1
ARG port=18080
FROM ${baseimage} as local-stage

# Docker image build arguments:
ARG awssdkversion=1.9.365


# Docker image build arguments:
ARG port

# Docker container environmental variables:
ENV VERSION_OVERRIDE="localdev"

# Ports:
EXPOSE ${port}

# Volumes:
VOLUME [ "/slate" ]

# Run once the container has started:
ENTRYPOINT [ "/bin/bash" ]

#######################################
## Build Stage                        #
#######################################
FROM ${baseimage} as build-stage

# Docker image build arguments:
ARG versionoverride="X.Y.Z"

# Docker container environmental variables:
ENV VERSION_OVERRIDE=${versionoverride}

# Set up custom yum repos:
# COPY ./yum.repos.d/aws-sdk.repo /etc/yum.repos.d/aws-sdk.repo

# Package installs/updates:
#RUN yum install epel-release -y

RUN dnf -y update \
 && dnf -y install openssh-server \
  make \
  autoconf \
  automake \
  dos2unix \
  ninja-build \
  gcc \
  gcc-c++ \
  gdb \
  clang \
  cmake \
  rsync \
  tar \
  passwd \
  python \
  boost \
  boost-devel \
  groff \
  less \
  which \
  cmake3 \
  zlib-devel \
  zlib \
  yaml-cpp\
  yaml-cpp-devel \
  openssl \
  openssl-devel \
  libcurl-devel \
  libcurl \
  cryptopp \
  strace \
  procps-ng \
  json-devel \
  protobuf-devel \
  protobuf-compiler \
  gmock \
  gmock-devel \
  gtest \
  gtest-devel \
  google-benchmark \
  google-benchmark-devel \
  perf \
  valgrind \
  valgrind-devel \
  && dnf clean all

# Install AWS CLI
RUN ln -s /usr/local/aws-cli/v2/current/bin/aws aws && \
    ln -s /usr/local/aws-cli/v2/current/bin/aws_completer aws_completer


# Install DynamoDB locally
RUN cd /tmp && \
    curl  https://s3.us-west-2.amazonaws.com/dynamodb-local/dynamodb_local_latest.tar.gz -o  db.tar.gz && \
    tar xvzf db.tar.gz && \
    rm db.tar.gz

# Set kernel
RUN ssh-keygen -A

RUN ( \
    echo 'LogLevel DEBUG2'; \
    echo 'PermitRootLogin yes'; \
    echo 'PasswordAuthentication yes'; \
    echo 'Subsystem sftp /usr/libexec/openssh/sftp-server'; \
  ) > /etc/ssh/sshd_config_test_clion

RUN useradd -m clionremote \
  && yes password | passwd clionremote

CMD ["/usr/sbin/sshd", "-D", "-e", "-f", "/etc/ssh/sshd_config_test_clion"]
