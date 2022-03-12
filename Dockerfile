# syntax=docker/dockerfile:1
######################################
# Build Stage                        #
######################################
FROM centos:7 as build-stage

# Docker image build arguments:
ARG awssdkversion=1.7.345

# Package installs/updates:
RUN yum install epel-release -y
RUN yum install boost-devel \
    cmake3 \
    gcc-c++.x86_64 \
    git \
    libcurl-devel \
    make \
    openssl-devel \
    yaml-cpp-devel \
    zlib-devel -y

# Download AWS C++ SDK
RUN curl -LO https://github.com/aws/aws-sdk-cpp/archive/${awssdkversion}.tar.gz && \
    tar xzf ${awssdkversion}.tar.gz

# Change working directory:
WORKDIR /build

# Build AWS C++ SDK
RUN cmake3 ../aws-sdk-cpp-${awssdkversion} -DBUILD_ONLY="dynamodb;route53" -DBUILD_SHARED_LIBS=Off && \
    make

#######################################
## Final Stage                        #
#######################################
FROM centos:7 as final-stage

# Docker image build arguments:
ARG awssdkversion=1.7.345

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

# Install AWS C++ SDK
COPY --from=build-stage /aws-sdk-cpp-${awssdkversion} /aws-sdk-cpp-${awssdkversion}
COPY --from=build-stage /build /build
RUN cd ./build && \
    make install

# Change working directory:
WORKDIR /work

# Volumes
VOLUME [ "/work" ]

# Run once the container has started:
ENTRYPOINT [ "/docker-entrypoint.sh" ]