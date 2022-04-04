# syntax=docker/dockerfile:1
FROM hub.opensciencegrid.org/slate/slate-client-server:1.0.1

# Docker image build arguments:
ARG helmversion
ARG kubectlversion

# Docker container environmental variables:
ENV DEBUG=True
ENV SLATE_VOLUME_DIR=/slate

# Set up custom yum repos:
COPY ./resources/docker/yum.repos.d/kubernetes.repo /etc/yum.repos.d/kubernetes.repo

# Package installs/updates:
RUN yum install epel-release -y
RUN yum install boost \
    kubectl-${kubectlversion} \
    unzip \
    which \
    yaml-cpp -y
RUN yum clean all && rm -rf /var/cache/yum

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

# Generate arbitrary encryption key:
RUN dd if=/dev/urandom of=/encryptionKey bs=1024 count=1

# Change working directory:
WORKDIR ${SLATE_VOLUME_DIR}

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "${SLATE_VOLUME_DIR}" ]

# Run once the container has started:
ENTRYPOINT ["/bin/bash"]