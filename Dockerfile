FROM alpine:latest AS build

LABEL maintainer="Liquidaty"
LABEL description="zsv: tabular data swiss-army knife CLI + world's fastest (simd) CSV parser"
LABEL url="https://github.com/liquidaty/zsv"

RUN apk add --no-cache gcc make musl-dev perl

WORKDIR /zsv
COPY . .

RUN \
    PREFIX=amd64-linux-musl \
    CC=gcc \
    MAKE=make \
    ARTIFACT_DIR=artifacts \
    RUN_TESTS=false \
    STATIC_BUILD=1 \
    SKIP_ZIP_ARCHIVE=true \
    SKIP_TAR_ARCHIVE=true \
    ./scripts/ci-build.sh

FROM scratch

WORKDIR /zsv
COPY --from=build /zsv/amd64-linux-musl/bin/zsv .
COPY --from=build /zsv/AUTHORS /zsv/LICENSE ./

ENTRYPOINT [ "./zsv" ]
