FROM alpine:latest
LABEL Name=anillo-os-builder Version=0.0.1
RUN apk add --no-cache clang lld python3 bash gcc libc-dev cmake make llvm-dev llvm-static qemu-img sgdisk dosfstools mtools git g++ fuse-dev util-linux-dev ninja
RUN cd /tmp && git clone https://github.com/braincorp/partfs.git && cd partfs && make && cp build/bin/partfs /usr/local/bin/ && cd / && rm -rf /tmp/partfs
RUN apk del fuse-dev util-linux-dev g++
RUN apk add --no-cache fuse util-linux
