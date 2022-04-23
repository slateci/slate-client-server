# syntax=docker/dockerfile:1

# Docker image build arguments:
ARG baseimage=hub.opensciencegrid.org/slate/slate-client-server:1.0.7
ARG port=18080

#######################################
## Local Stage                        #
#######################################
FROM ${baseimage} as local-stage

# Docker image build arguments:
ARG port

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

# Change working directory:
WORKDIR /slate

# Copy in all source material:
COPY ./ .

# Remove any existing build artifacts:
RUN rm -rf /slate/build/*

# Build slate-service:
RUN chmod +x ./resources/docker/scripts/build.sh && \
    . ./resources/docker/scripts/build.sh

#######################################
## Release Stage                      #
#######################################

FROM centos:7 as release-stage

# Docker image build arguments:
ARG awssdkversion=1.7.25
ARG port

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
COPY --from=build-stage /usr/local/bin/helm helm

# Change working directory:
WORKDIR /usr/bin

# Install Kubectl:
COPY --from=build-stage /usr/bin/kubectl kubectl

# Install slate-service:
COPY --from=build-stage /slate/build/slate-service slate-service

# Change working directory:
WORKDIR /slate

# Create slate directories:
RUN mkdir /conf /keys /users

# Change working directory:
WORKDIR /

# Prepare entrypoint:
COPY --from=build-stage /slate/resources/docker/scripts/docker-entrypoint.sh /
RUN chmod +x /docker-entrypoint.sh

# Ports:
EXPOSE ${port}

# Run once the container has started:
ENTRYPOINT [ "/docker-entrypoint.sh" ]
