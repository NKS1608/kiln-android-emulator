# Build environment for the kiln guest kernel module (native arm64, clang).
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates git make bc bison flex cpio kmod python3 perl \
      clang lld llvm libelf-dev libdw-dev libssl-dev dwarves \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /build
