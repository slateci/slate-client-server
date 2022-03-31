# syntax=docker/dockerfile:1
FROM hub.opensciencegrid.org/slate/slate-client-server:1.0.1

# Docker image build arguments:
ARG horkle='snorkle'

# Docker container environmental variables:
ENV DBUILD_CLIENT=False
ENV DBUILD_SERVER=True
ENV DBUILD_SERVER_TESTS=True
ENV DSTATIC_CLIENT=False
ENV DEBUG=FALSE
ENV HELM_VERSION=3.8.1
ENV KUBECTL_VERSION=1.21.11

# Set up custom yum repos:
COPY ./resources/docker/yum.repos.d/* /etc/yum.repos.d/

# Package installs/upates:
RUN yum install epel-release -y
RUN yum install boost kubectl-${KUBECTL_VERSION} which yaml-cpp -y
RUN yum clean all && rm -rf /var/cache/yum

# Install Helm3:
RUN curl -LO https://get.helm.sh/helm-v${HELM_VERSION}-linux-amd64.tar.gz && \
    tar xzf helm-v${HELM_VERSION}-linux-amd64.tar.gz && \
    mv linux-amd64/helm /usr/local/bin/helm && \
    rm -rf helm-v${HELM_VERSION}-linux-amd64.tar.gz linux-amd64

# Generate arbitrary encryption key:
RUN head -c 1024 /dev/urandom > /encrpytionKey

# Change working directory:
WORKDIR /work

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "/work" ]

# Run once the container has started:
ENTRYPOINT ["/bin/bash"]