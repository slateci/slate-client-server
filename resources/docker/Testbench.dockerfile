# syntax=docker/dockerfile:1
FROM rockylinux/rockylinux:8

# Docker container environmental variables:
ENV DEBUG=xxxx
ENV HELM_VERSION=xxxx
ENV HISTFILE=/work/.bash_history_docker
ENV KUBECTL_VERSION=xxxx
ENV SLATE_CLI_TOKEN=xxxx
ENV SLATE_API_ENDPOINT=xxxx

# Set up custom yum repos:
COPY ./resources/docker/yum.repos.d/kubernetes.repo /etc/yum.repos.d/kubernetes.repo

RUN echo "The env variables are: DEBUG=${DEBUG}, HELM_VERSION=${HELM_VERSION}, etc."

# Package installs/updates:
RUN yum install epel-release -y
RUN yum install bind-utils \
    kubectl-${KUBECTL_VERSION} \
    ncurses \
    openssh-clients \
    which -y
RUN yum clean all && rm -rf /var/cache/yum

# Download and install the SLATE CLI:
RUN curl -LO https://jenkins.slateci.io/artifacts/client/slate-linux.tar.gz && \
    curl -LO https://jenkins.slateci.io/artifacts/client/slate-linux.sha256
RUN sha256sum -c slate-linux.sha256 || exit 1
RUN tar xzvf slate-linux.tar.gz && \
    mv slate /usr/bin/slate && \
    rm slate-linux.tar.gz slate-linux.sha256

# Install AWS CLI (for debugging)
RUN curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip" && \
    unzip awscliv2.zip && \
    ./aws/install

# Install Helm3:
RUN curl -LO https://get.helm.sh/helm-v${HELM_VERSION}-linux-amd64.tar.gz && \
    curl -LO https://get.helm.sh/helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum
RUN sha256sum -c helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum || exit 1
RUN tar xzf helm-v${HELM_VERSION}-linux-amd64.tar.gz && \
    mv linux-amd64/helm /usr/local/bin/helm && \
    rm -rf helm-v${HELM_VERSION}-linux-amd64.tar.gz helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum linux-amd64

# Prepare entrypoint:
COPY ./resources/docker/scripts/testbench-start.sh ./
RUN chmod +x ./testbench-start.sh

# Set SLATE home:
RUN mkdir -p -m 0700 ./.slate

# Set the token:
RUN echo ${SLATE_CLI_TOKEN} > ./.slate/token && \
    chmod 600 ./.slate/token

# Change working directory:
WORKDIR /work

# Volumes
VOLUME [ "/work" ]

# Run once the container has started:
ENTRYPOINT ["testbench-start.sh"]