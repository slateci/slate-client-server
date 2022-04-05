# syntax=docker/dockerfile:1

# Docker image build arguments:
ARG baseimage=hub.opensciencegrid.org/slate/slate-client-server:1.0.5

#######################################
## Local Stage                        #
#######################################
FROM ${baseimage} as local-stage

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "/slate" ]

# Run once the container has started:
ENTRYPOINT [ "/bin/bash" ]

#######################################
## Build Stage                        #
#######################################
FROM ${baseimage} as build-stage

# Change working directory:
WORKDIR /slate

# Copy in all source material:
COPY ./ .

# Remove any existing build artifacts:
RUN rm -rf /slate/build/*

# Change working directory:
WORKDIR /slate/build

# Build slate-service:
RUN cmake3 .. -DBUILD_CLIENT=False -DBUILD_SERVER=True -DBUILD_SERVER_TESTS=True -DSTATIC_CLIENT=False && \
    make

#######################################
## Release Stage                      #
#######################################

FROM centos:7 as release-stage

# Docker image build arguments:
ARG awssdkversion=1.7.25

# Set up custom yum repos:
COPY --from=build-stage /slate/resources/docker/yum.repos.d/aws-sdk.repo /etc/yum.repos.d/aws-sdk.repo

# Package installs/updates:
RUN yum install epel-release -y
RUN yum install aws-sdk-cpp-dynamodb-libs-${awssdkversion} \
    aws-sdk-cpp-route53-libs-${awssdkversion} \
    boost \
    glibc \
    groff \
    less \
    which \
    yaml-cpp -y
RUN yum clean all && rm -rf /var/cache/yum

# Change working directory:
WORKDIR /usr/local/bin

# Install AWS CLI
COPY --from=build-stage /usr/local/aws-cli /usr/local/aws-cli
RUN ln -s /usr/local/aws-cli/v2/current/bin/aws aws && \
    ln -s /usr/local/aws-cli/v2/current/bin/aws_completer aws_completer

# Install Helm
COPY --from=build-stage /usr/local/bin/helm /usr/local/bin/helm

# Install Kubectl
COPY --from=build-stage /usr/bin/kubectl /usr/bin/kubectl

# Install slate-service
COPY --from=build-stage /slate/build/slate-service /usr/bin/slate-service

# Change working directory:
WORKDIR /

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "/slate" ]

# Run once the container has started:
#ENTRYPOINT ["/usr/bin/slate-service --config /slate/slate.conf"]
ENTRYPOINT [ "/bin/bash" ]