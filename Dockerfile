# syntax=docker/dockerfile:1
FROM centos:7

# Docker container environmental variables:
ENV DEBUG=False

# Set up custom yum repos:
COPY ./resources/docker/kubernetes.repo /etc/yum.repos.d/kubernetes.repo

# Package installs/updates:
RUN yum install epel-release -y
RUN yum install boost-devel \
    cmake3 \
    gcc-c++.x86_64 \
    libcurl-devel \
    kubectl \
    make \
    openssl-devel \
    yaml-cpp-devel \
    zlib-devel -y

# Prepare entrypoint:
COPY ./docker-entrypoint.sh ./
RUN chmod +x ./docker-entrypoint.sh

# Change working directory:
WORKDIR /work

# Volumes
VOLUME [ "/work" ]

# Run once the container has started:
ENTRYPOINT [ "/docker-entrypoint.sh" ]