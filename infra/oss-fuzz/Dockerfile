# Dockerfile for OSS-Fuzz integration.
#
# This file is intended to be copied (or referenced) by a corresponding
# projects/zsv/Dockerfile in the upstream google/oss-fuzz repository. It
# builds a libFuzzer harness for zsv's fast CSV scan engine.
FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y --no-install-recommends \
      make autoconf automake libtool ca-certificates git \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/liquidaty/zsv.git
WORKDIR $SRC/zsv
COPY build.sh $SRC/
