# syntax=docker/dockerfile:1
FROM hub.opensciencegrid.org/slate/slate-client-server:1.0.5 as build-stage

# Change working directory:
WORKDIR /slate

# Copy in all source material:
COPY ./ .

# Prepare API script:
RUN chmod +x ./resources/docker/scripts/start-api.sh

# Change working directory:
WORKDIR /slate/build

# Build slate-service:
RUN cmake3 .. -DBUILD_CLIENT=False -DBUILD_SERVER=True -DBUILD_SERVER_TESTS=True -DSTATIC_CLIENT=False && \
    make

# Ports:
EXPOSE 18080

# Volumes:
VOLUME [ "/slate/build" ]

# Run once the container has started:
ENTRYPOINT [ "/bin/bash" ]

#FROM centos:7 as release-stage

#WORKDIR /usr/local/bin

# Install AWS CLI
#COPY --from=build-stage /usr/local/aws-cli /usr/local/aws-cli
#RUN ln -s aws /usr/local/aws-cli/v2/current/bin/aws && \
#    ln -s aws_completer /usr/local/aws-cli/v2/current/bin/aws_completer

# Install Helm
# COPY --from=build-stage /usr/local/bin/helm /usr/local/bin/helm

# Install Kubectl
# COPY --from=build-stage /usr/bin/kubectl /usr/bin/kubectl