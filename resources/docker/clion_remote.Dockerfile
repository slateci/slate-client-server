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
ARG baseimage=hub.opensciencegrid.org/slate/slate-client-server:1.0.7
ARG port=18080
FROM ${baseimage} as local-stage

# Docker image build arguments:
ARG awssdkversion=1.7.25


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
RUN yum install epel-release -y

RUN yum -y update \
 && yum -y install aws-sdk-cpp-dynamodb-libs-${awssdkversion} \
  aws-sdk-cpp-route53-libs-${awssdkversion} \
  openssh-server \
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
  aws-sdk-cpp-dynamodb-libs-${awssdkversion} \
  aws-sdk-cpp-route53-libs-${awssdkversion} \
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
  openssl-static \
  openssl-devel \
  libcurl-devel \
  libcurl \
  cryptopp \
  strace \
  centos-release-scl \
  &&  yum install -y devtoolset-7 \
  && yum clean all

# Install AWS CLI
RUN ln -s /usr/local/aws-cli/v2/current/bin/aws aws && \
    ln -s /usr/local/aws-cli/v2/current/bin/aws_completer aws_completer

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