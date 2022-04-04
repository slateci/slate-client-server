# syntax=docker/dockerfile:1
FROM centos:7

# Docker image build arguments:
ARG helmversion
ARG kubectlversion
ARG slateapiversion
ARG slatevolumedir

# Docker container environmental variables:
ENV DEBUG=False

# Set up custom yum repos:
COPY ./resources/docker/yum.repos.d/* /etc/yum.repos.d/

# Package installs/updates:
RUN yum install epel-release -y
RUN yum install boost \
    glibc \
    groff \
    kubectl-${kubectlversion} \
    less \
    slate-api-server-${slateapiversion} \
    unzip \
    which \
    yaml-cpp -y
RUN yum clean all && rm -rf /var/cache/yum

# Install AWS CLI (for debugging)
RUN curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip" && \
    unzip awscliv2.zip && \
    ./aws/install

# Install Helm3:
RUN curl -LO https://get.helm.sh/helm-v${helmversion}-linux-amd64.tar.gz && \
    curl -LO https://get.helm.sh/helm-v${helmversion}-linux-amd64.tar.gz.sha256sum
RUN sha256sum -c helm-v${helmversion}-linux-amd64.tar.gz.sha256sum || exit 1
RUN tar xzf helm-v${helmversion}-linux-amd64.tar.gz && \
    mv linux-amd64/helm /usr/local/bin/helm && \
    rm -rf helm-v${helmversion}-linux-amd64.tar.gz helm-v${helmversion}-linux-amd64.tar.gz.sha256sum linux-amd64

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "${slatevolumedir}" ]

# Run once the container has started:
ENTRYPOINT ["/usr/bin/slate-service --config ${slatevolumedir}/slate.conf --encryptionKeyFile ${slatevolumedir}/encryptionKey"]