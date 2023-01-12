# syntax=docker/dockerfile:1
FROM hub.opensciencegrid.org/slate/slate-client-server:2.1.0

# Docker image build arguments:
ARG apiport=18080
ARG projectpath=/tmp/work
ARG sshloglevel=DEBUG2
ARG sshpassword=password
ARG sshuser=clionremote
ARG versionoverride="localdev"

# Docker container environmental variables:
ENV DYNAMODB_JAR=/dynamodb/DynamoDBLocal.jar
ENV DYNAMODB_LIB=/dynamodb/DynamoDBLocal_lib
ENV KUBECONFIG=/output/kubeconfig.yaml
ENV SLATE_SCHEMA_DIR=${projectpath}/resources/api_specification
ENV TEST_SRC=${projectpath}/test
ENV VERSION_OVERRIDE=${versionoverride}

# Package installs/updates:
RUN dnf update -y && \
    dnf install -y \
      autoconf \
      automake \
      boost \
      boost-devel \
      clang \
      cmake \
      dos2unix \
      gdb \
      google-benchmark \
      google-benchmark-devel \
      groff \
      less \
      ninja-build \
      openssh-server \
      passwd \
      perf \
      python \
      tar \
      rsync \
      yaml-cpp \
      valgrind \
      valgrind-devel \
      zlib && \
    dnf clean all && \
    rm -rf /var/cache/yum

# Generate new host keys:
RUN ssh-keygen -A

# Set up SSH config and user:
RUN ( \
    echo "LogLevel ${sshloglevel}"; \
    echo 'PermitRootLogin yes'; \
    echo 'PasswordAuthentication yes'; \
    echo 'Subsystem sftp /usr/libexec/openssh/sftp-server'; \
  ) > /etc/ssh/sshd_config_test_clion

RUN useradd -m ${sshuser} && \
    yes ${sshpassword} | passwd ${sshuser}

# Ports:
EXPOSE 22 ${apiport}

# Run once the container has started:
CMD ["/usr/sbin/sshd", "-D", "-e", "-f", "/etc/ssh/sshd_config_test_clion"]
