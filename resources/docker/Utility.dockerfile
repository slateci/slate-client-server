# syntax=docker/dockerfile:1
FROM centos:7

# Docker image build arguments:
ARG helmversion
ARG kubectlversion
ARG javaversion
ARG slateapitoken
ARG slateapiuser

# Docker container environmental variables:
ENV HISTFILE=/slate/.bash_history_docker
ENV SLATE_API_ENDPOINT=http://slate_api:18080
ENV SLATE_API_USER=${slateapiuser}

# Set up custom yum repos:
COPY ./resources/docker/yum.repos.d/kubernetes.repo /etc/yum.repos.d/kubernetes.repo

# Package installs/updates:
RUN yum install epel-release -y
RUN yum install bash-completion \
    bind-utils \
    git \
    java-${javaversion} \
    kubectl-${kubectlversion} \
    ncurses \
    net-tools \
    openssh-clients \
    unzip \
    which -y
RUN yum clean all && rm -rf /var/cache/yum

# Install kubectl bash completion:
RUN echo 'source <(kubectl completion bash)' >>~/.bashrc

# Download and install the SLATE CLI:
RUN curl -LO https://jenkins.slateci.io/artifacts/client/slate-linux.tar.gz && \
    curl -LO https://jenkins.slateci.io/artifacts/client/slate-linux.sha256
RUN sha256sum -c slate-linux.sha256 || exit 1
RUN tar xzvf slate-linux.tar.gz && \
    mv slate /usr/bin/slate && \
    rm slate-linux.tar.gz slate-linux.sha256

# Change working directory:
WORKDIR /tmp

# Install AWS CLI (for debugging)
RUN curl -LO https://raw.githubusercontent.com/slateci/docker-images/stable/slate-client-server/scripts/install-aws-cli.sh
RUN chmod +x ./install-aws-cli.sh && \
    ./install-aws-cli.sh && \
    rm ./install-aws-cli.sh

# Install Helm3:
RUN curl -LO https://raw.githubusercontent.com/slateci/docker-images/stable/slate-client-server/scripts/install-helm.sh
RUN chmod +x ./install-helm.sh && \
    ./install-helm.sh ${helmversion} && \
    rm ./install-helm.sh

# Change working directory:
WORKDIR /

# Set SLATE home:
RUN mkdir -p -m 0755 ${HOME}/.slate

# Set the token:
RUN echo ${slateapitoken} > ${HOME}/.slate/token && \
    chmod 600 ${HOME}/.slate/token

# Change working directory:
WORKDIR /slate

# Volumes
VOLUME [ "/slate" ]

# Run once the container has started:
ENTRYPOINT ["/bin/bash"]