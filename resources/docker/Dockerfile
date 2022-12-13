# syntax=docker/dockerfile:1

# Docker image build arguments:
ARG baseimage=hub.opensciencegrid.org/slate/slate-client-server:2.0.4
ARG port=18080

#######################################
## Local Stage                        #
#######################################
FROM ${baseimage} as local-stage

# Docker image build arguments:
ARG port

# Docker container environmental variables:
ENV VERSION_OVERRIDE="localdev"

# Ports:
EXPOSE ${port}

# Volumes:
VOLUME [ "/slate" ]

# Run once the container has started:
ENTRYPOINT [ "/bin/bash" ]

#######################################
## Build Stage                        #
#######################################
FROM ${baseimage} as build-stage

# Docker image build arguments:
ARG versionoverride="X.Y.Z"

# Docker container environmental variables:
ENV VERSION_OVERRIDE=${versionoverride}

# Change working directory:
WORKDIR /slate

# Copy in all source material:
COPY ./ .

# Remove any existing build artifacts:
RUN rm -rf /slate/build/*

# Build slate-service:
RUN chmod +x ./resources/docker/scripts/build.sh && \
    . ./resources/docker/scripts/build.sh

#######################################
## Release Stage                      #
#######################################
# TODO: Return to a more light-weight Rocky9 runtime container. The current base image has a lot of extra stuff in it like DynamoDB.
FROM ${baseimage} as release-stage

# Docker image build arguments:
ARG port

# Change working directory:
WORKDIR /usr/local/bin

# Install slate-service:
COPY --from=build-stage /slate/build/slate-service slate-service

# Change working directory:
WORKDIR /slate

# Create slate directories:
RUN mkdir ./conf ./keys ./users

# Change working directory:
WORKDIR /

# Prepare entrypoint:
COPY --from=build-stage /slate/resources/docker/scripts/docker-entrypoint.sh /
RUN chmod +x /docker-entrypoint.sh

# Ports:
EXPOSE ${port}

# Run once the container has started:
ENTRYPOINT [ "/docker-entrypoint.sh" ]
