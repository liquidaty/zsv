FROM alpine:latest AS build

LABEL maintainer="Liquidaty"
LABEL url="https://github.com/liquidaty/zsv"
LABEL org.opencontainers.image.description="zsv: tabular data swiss-army knife CLI + world's fastest (simd) CSV parser"

# patch: applied to the vendored jq source by app/Makefile's ${JQ_SRC} rule (Alpine
# ships neither a patch package nor a busybox patch applet by default).
# git: `git apply` builds a test fixture in app/test/parallel/Makefile, and this
# image runs the test suite (RUN_TESTS=true below).
# Both are already in the ci.yml alpine job's apk list.
RUN apk add bash gcc make musl-dev ncurses-dev ncurses-static tmux file sqlite curl zip patch git

WORKDIR /zsv
COPY . .

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
