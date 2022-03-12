#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Building:
cd /work/build
echo "Using the cmak3 arguments: ${CMAKE_ARGS}..."
cmake3 .. ${CMAKE_ARGS}

${1:-/usr/bin/make}

# Instructions (place in README.md at some point)
#
#
# Build the entire image:
#
# $ docker build --file Dockerfile --tag slate-client-server:local .
#
# Build the final-stage:
#
# $ docker build --file Dockerfile --target final-stage --tag slate-client-server:local .
#
#
# Run the image to generate build artifacts:
#
# $ docker run -it -v ${PWD}:/work --env CMAKE_ARGS="-DBUILD_CLIENT=False -DBUILD_SERVER=True -DBUILD_SERVER_TESTS=True -DSTATIC_CLIENT=False" slate-client-server:local
#
# Alternatively run a shell in the container:
# $ docker run -it -v ${PWD}:/work --env CMAKE_ARGS="-DBUILD_CLIENT=False -DBUILD_SERVER=True -DBUILD_SERVER_TESTS=True -DSTATIC_CLIENT=False" slate-client-server:local bash
#
# and execute make yourself:
# [root@78a3ed63cf61 build]# make