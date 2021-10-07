FROM ubuntu:latest
LABEL Name=anillo-os-builder Version=0.0.1
ARG DEBIAN_FRONTEND="noninteractive"
ENV TZ=America/New_York
RUN apt-get -y update
RUN apt-get -y install wget gnupg
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN echo 'deb http://apt.llvm.org/focal/ llvm-toolchain-focal main' >> /etc/apt/sources.list.d/llvm.list && echo 'deb-src http://apt.llvm.org/focal/ llvm-toolchain-focal main' >> /etc/apt/sources.list.d/llvm.list
RUN apt-get -y update
RUN apt-get -y install build-essential python3 cmake clang lld llvm-dev qemu-utils gdisk dosfstools mtools git libfuse-dev libfdisk-dev ninja-build
RUN cd /tmp && git clone https://github.com/braincorp/partfs.git && cd partfs && make && cp build/bin/partfs /usr/local/bin/ && cd / && rm -rf /tmp/partfs
