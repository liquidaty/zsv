FROM alpine:latest AS build

LABEL maintainer="Liquidaty"
LABEL url="https://github.com/liquidaty/zsv"
LABEL org.opencontainers.image.description="zsv: tabular data swiss-army knife CLI + world's fastest (simd) CSV parser"

RUN apk update && apk add bash make musl-dev ncurses-dev ncurses-static tmux file sqlite curl zip git

# GCC 15 onwards causes failure due to the default strict aliasing rules in the compiler.
# GCC 14 is the last version that works with zsv, so we need to install it from the v3.22 repository. 
RUN apk add gcc=14.2.0-r6 --repository=https://dl-cdn.alpinelinux.org/alpine/v3.22/main

WORKDIR /zsv
COPY . .

# For some reason, the diff file is not being applied correctly in the docker build context.
# git apply on local docker build fails to resolve the full path for parallel tests.
# So, we need to remove the prefix to make paths relative in the diff file before applying it.
RUN sed "s|app/test/parallel/||" -i app/test/parallel/chunk_break.diff

RUN mkdir /usr/local/etc

RUN \
    PREFIX=amd64-linux-musl \
    CC=gcc \
    CFLAGS='-DPREFIX=\"\"' \
    MAKE=make \
    ARTIFACT_DIR=artifacts \
    RUN_TESTS=true \
    STATIC_BUILD=1 \
    SKIP_ZIP_ARCHIVE=true \
    SKIP_TAR_ARCHIVE=true \
    ./scripts/ci-build.sh

FROM scratch

WORKDIR /zsv
COPY --from=build /zsv/amd64-linux-musl/bin/zsv .
COPY --from=build /zsv/AUTHORS /zsv/LICENSE ./

ENTRYPOINT [ "./zsv" ]
