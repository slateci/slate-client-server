# syntax=docker/dockerfile:1
FROM rockylinux/rockylinux:8

# Docker image build arguments:
ARG helmversion
ARG kubectlversion
ARG slateapitoken
ARG slateapiuser

# Docker container environmental variables:
ENV DEBUG=True
ENV HISTFILE=/work/.bash_history_docker
ENV SLATE_API_ENDPOINT=http://slate_api_server:18080
ENV SLATE_API_USER=${slateapiuser}

# Set up custom yum repos:
COPY ./resources/docker/yum.repos.d/kubernetes.repo /etc/yum.repos.d/kubernetes.repo

# Package installs/updates:
RUN dnf install epel-release -y
RUN dnf install bind-utils \
    kubectl-${kubectlversion} \
    ncurses \
    openssh-clients \
    unzip \
    which -y
RUN dnf clean all && rm -rf /var/cache/yum

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
    ./aws/install && \
    rm awscliv2.zip

# Install Helm3:
RUN curl -LO https://get.helm.sh/helm-v${helmversion}-linux-amd64.tar.gz && \
    curl -LO https://get.helm.sh/helm-v${helmversion}-linux-amd64.tar.gz.sha256sum
RUN sha256sum -c helm-v${helmversion}-linux-amd64.tar.gz.sha256sum || exit 1
RUN tar xzf helm-v${helmversion}-linux-amd64.tar.gz && \
    mv linux-amd64/helm /usr/local/bin/helm && \
    rm -rf helm-v${helmversion}-linux-amd64.tar.gz helm-v${helmversion}-linux-amd64.tar.gz.sha256sum linux-amd64

# Prepare entrypoint:
COPY ./resources/docker/scripts/start-testbench.sh ./
RUN chmod +x ./start-testbench.sh

# Set SLATE home:
RUN mkdir -p -m 0755 ${HOME}/.slate

# Set the token:
RUN echo ${slateapitoken} > ${HOME}/.slate/token && \
    chmod 600 ${HOME}/.slate/token

# Change working directory:
WORKDIR /work

# Volumes
VOLUME [ "/work" ]

# Run once the container has started:
ENTRYPOINT ["/start-testbench.sh"]