FROM alpine:latest AS build

LABEL maintainer="Liquidaty"
LABEL url="https://github.com/liquidaty/zsv"
LABEL org.opencontainers.image.description="zsv: tabular data swiss-army knife CLI + world's fastest (simd) CSV parser"

RUN apk add bash gcc make musl-dev ncurses-dev ncurses-static tmux file sqlite curl zip ccache

WORKDIR /zsv
COPY . .

RUN mkdir /usr/local/etc

RUN \
    PREFIX=amd64-linux-musl \
    CC=gcc \
    MAKE=make \
    MAKE_PARALLEL=true \
    ENABLE_CCACHE=true \
    RUN_TESTS=true \
    STATIC_BUILD=1 \
    ARTIFACT_DIR=artifacts \
    SKIP_ZIP_ARCHIVE=true \
    SKIP_TAR_ARCHIVE=true \
    ./scripts/ci-build.sh

FROM scratch

WORKDIR /zsv
COPY --from=build /zsv/amd64-linux-musl/bin/zsv .
COPY --from=build /zsv/AUTHORS /zsv/LICENSE ./

ENTRYPOINT [ "./zsv" ]
