# syntax=docker/dockerfile:1
FROM hub.opensciencegrid.org/slate/slate-client-server:1.0.1

# Docker container environmental variables:
ENV DEBUG=xxxx
ENV HELM_VERSION=xxxx
ENV KUBECTL_VERSION=xxxx

# Set up custom yum repos:
COPY ./resources/docker/yum.repos.d/kubernetes.repo /etc/yum.repos.d/kubernetes.repo

# Package installs/upates:
RUN yum install epel-release -y
RUN yum install boost \
    kubectl-${KUBECTL_VERSION} \
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
    curl -LO https://get.helm.sh/helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum
RUN sha256sum -c helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum || exit 1
RUN tar xzf helm-v${HELM_VERSION}-linux-amd64.tar.gz && \
    mv linux-amd64/helm /usr/local/bin/helm && \
    rm -rf helm-v${HELM_VERSION}-linux-amd64.tar.gz helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum linux-amd64

# Generate arbitrary encryption key:
RUN dd if=/dev/urandom of=/encryptionKey bs=1024 count=1

# Change working directory:
WORKDIR /slate

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "/slate" ]

# Run once the container has started:
ENTRYPOINT ["/bin/bash"]