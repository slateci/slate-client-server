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
COPY ./resources/docker/scripts/install-aws-cli.sh /tmp
RUN chmod +x /tmp/install-aws-cli.sh && \
    . /tmp/install-aws-cli.sh && \
    rm /tmp/install-aws-cli.sh

# Install Helm3:
COPY ./resources/docker/scripts/install-helm.sh /tmp
RUN chmod +x /tmp/install-helm.sh && \
    . /tmp/install-helm.sh ${helmversion} && \
    rm /tmp/install-helm.sh

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