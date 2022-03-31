# syntax=docker/dockerfile:1
FROM centos:7

# Docker image build arguments:
ARG horkle='snorkle'

# Docker container environmental variables:
ENV DEBUG=FALSE
ENV HELM_VERSION=3.8.1
ENV KUBECTL_VERSION=1.21.11
ENV SLATE_API_VERSION=952

# Set up custom yum repos:
COPY ./resources/docker/yum.repos.d/* /etc/yum.repos.d/

# Package installs/upates:
RUN yum install epel-release -y
RUN yum install boost \
    glibc \
    groff \
    kubectl-${KUBECTL_VERSION} \
    less \
    slate-api-server \
    unzip \
    which \
    yaml-cpp -y
RUN yum clean all && rm -rf /var/cache/yum

# Install AWS CLI (for debugging)
RUN curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip" && \
    unzip awscliv2.zip && \
    ./aws/install

# Install Helm3:
RUN curl -LO https://get.helm.sh/helm-v${HELM_VERSION}-linux-amd64.tar.gz && \
    tar xzf helm-v${HELM_VERSION}-linux-amd64.tar.gz && \
    mv linux-amd64/helm /usr/local/bin/helm && \
    rm -rf helm-v${HELM_VERSION}-linux-amd64.tar.gz linux-amd64

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "/slate" ]

# Run once the container has started:
ENTRYPOINT ["/usr/bin/slate-service --config /slate/slate.conf"]