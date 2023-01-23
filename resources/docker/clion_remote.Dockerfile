# syntax=docker/dockerfile:1
FROM hub.opensciencegrid.org/slate/slate-client-server:2.1.0

# Docker image build arguments:
ARG apiport=18080
ARG kubeconfigpath=/kubernetes/kubeconfig.yaml
ARG projectpath=/tmp/work
ARG sshloglevel=DEBUG2
ARG sshpassword=password
ARG versionoverride="localdev"

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

# Set up SSH config:
RUN ( \
    echo "LogLevel ${sshloglevel}"; \
    echo 'PermitRootLogin yes'; \
    echo 'PasswordAuthentication yes'; \
    echo 'Subsystem sftp /usr/libexec/openssh/sftp-server'; \
  ) > /etc/ssh/sshd_config_test_clion

RUN echo ${sshpassword} | passwd --stdin root

# Set up env vars for all users:
RUN ( \
    echo "export DYNAMODB_JAR=/dynamodb/DynamoDBLocal.jar" \
    echo "export DYNAMODB_LIB=/dynamodb/DynamoDBLocal_lib" \
    echo "export KUBECONFIG=${kubeconfigpath}" \
    echo "export SLATE_SCHEMA_DIR=${projectpath}/resources/api_specification" \
    echo "export TEST_SRC=${projectpath}/test" \
    echo "export VERSION_OVERRIDE=${versionoverride}" \
  ) > /etc/profile.d/global-envs.sh

# Ports:
EXPOSE 22 ${apiport}

# Run once the container has started:
CMD ["/usr/sbin/sshd", "-D", "-e", "-f", "/etc/ssh/sshd_config_test_clion"]
