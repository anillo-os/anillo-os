FROM ubuntu:latest
LABEL Name=anillo-os-builder Version=0.0.1
ARG DEBIAN_FRONTEND="noninteractive"
ENV TZ=America/New_York
RUN apt-get -y update
RUN apt-get -y install wget gnupg
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
RUN echo 'deb http://apt.llvm.org/focal/ llvm-toolchain-focal main' >> /etc/apt/sources.list.d/llvm.list && echo 'deb-src http://apt.llvm.org/focal/ llvm-toolchain-focal main' >> /etc/apt/sources.list.d/llvm.list
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main' | tee /etc/apt/sources.list.d/kitware.list >/dev/null
RUN apt-get -y update
RUN rm /usr/share/keyrings/kitware-archive-keyring.gpg
RUN apt-get -y install kitware-archive-keyring
RUN apt-get -y install build-essential python3 cmake clang lld llvm-dev qemu-utils gdisk dosfstools mtools git libfuse-dev libfdisk-dev ninja-build
RUN cd /tmp && git clone https://github.com/braincorp/partfs.git && cd partfs && make && cp build/bin/partfs /usr/local/bin/ && cd / && rm -rf /tmp/partfs
