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

# Run once the container has started:
ENTRYPOINT ["/bin/bash"]