# syntax=docker/dockerfile:1
FROM centos:7

# Docker image build arguments:
ARG helmversion=3.8.1
ARG kubectlversion=1.21.11
ARG slateapiversion=952
ARG slatevolumedir=/slate

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
COPY ./resources/docker/scripts/install-aws-cli.sh /tmp
RUN chmod +x /tmp/install-aws-cli.sh && \
    . /tmp/install-aws-cli.sh && \
    rm /tmp/install-aws-cli.sh

# Install Helm3:
COPY ./resources/docker/scripts/install-helm.sh /tmp
RUN chmod +x /tmp/install-helm.sh && \
    . /tmp/install-helm.sh ${helmversion} && \
    rm /tmp/install-helm.sh

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "${slatevolumedir}" ]

# Run once the container has started:
ENTRYPOINT ["/usr/bin/slate-service --config ${slatevolumedir}/slate.conf --encryptionKeyFile ${slatevolumedir}/encryptionKey"]