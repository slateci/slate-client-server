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
ARG awssdkversion=1.7.345
ARG port

# Package installs/updates:
RUN yum install -y epel-release
RUN yum install -y boost-devel \
   cmake3 \
   gcc-c++.x86_64 \
   less \
   libcurl-devel \
   make \
   openssl-devel \
   perl-Digest-SHA \
   unzip \
   which \
   yaml-cpp-devel \
   zlib-devel
RUN yum clean all && rm -rf /var/cache/yum

# Install AWS C++ SDK
COPY --from=build-stage /aws-sdk-cpp-${awssdkversion} /aws-sdk-cpp-${awssdkversion}
COPY --from=build-stage /build /build
RUN cd ./build && \
    make install && \
    make clean
RUN rm -rf /aws-sdk-cpp-${awssdkversion} /build

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
RUN mkdir ./conf ./keys ./users

# Change working directory:
WORKDIR /

# Prepare entrypoint:
COPY --from=build-stage /slate/resources/docker/scripts/docker-entrypoint.sh /
RUN chmod +x /docker-entrypoint.sh

# Ports:
EXPOSE ${port}

# Run once the container has started:
ENTRYPOINT [ "/docker-entrypoint.sh" ]
