FROM ubuntu:latest
LABEL Name=anillo-os-builder Version=0.0.1
ARG DEBIAN_FRONTEND="noninteractive"
ENV TZ=America/New_York
RUN apt-get -y update
RUN apt-get -y install wget gnupg
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
RUN echo 'deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy main' >> /etc/apt/sources.list.d/llvm.list && echo 'deb-src http://apt.llvm.org/jammy/ llvm-toolchain-jammy main' >> /etc/apt/sources.list.d/llvm.list
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main' | tee /etc/apt/sources.list.d/kitware.list >/dev/null
RUN apt-get -y update
RUN rm /usr/share/keyrings/kitware-archive-keyring.gpg
RUN apt-get -y install kitware-archive-keyring
RUN apt-get -y install build-essential
RUN apt-get -y install python3 python3-lark
RUN apt-get -y install cmake
RUN apt-get -y install clang
RUN apt-get -y install lld
RUN apt-get -y install llvm-dev
RUN apt-get -y install qemu-utils
RUN apt-get -y install gdisk
RUN apt-get -y install dosfstools
RUN apt-get -y install mtools
RUN apt-get -y install git
RUN apt-get -y install libfuse-dev
RUN apt-get -y install libfdisk-dev
RUN apt-get -y install ninja-build
RUN apt-get -y install libc6-dev-i386
RUN apt-get -y install fuse
RUN cd /tmp && git clone https://github.com/braincorp/partfs.git && cd partfs && make -j && cp build/bin/partfs /usr/local/bin/ && cd / && rm -rf /tmp/partfs
RUN cd /tmp && git clone https://github.com/tpoechtrager/cctools-port.git && cd cctools-port/cctools && ./configure --prefix=/usr/local --target=x86_64-apple-darwin11 --with-llvm-config=llvm-config && make -j && make install && cd / && rm -rf /tmp/cctools-port
RUN cd /tmp && git clone https://github.com/tpoechtrager/cctools-port.git && cd cctools-port/cctools && ./configure --prefix=/usr/local --target=aarch64-apple-darwin11 --with-llvm-config=llvm-config && make -j && make install && cd / && rm -rf /tmp/cctools-port
RUN update-alternatives --install /usr/bin/lld-link lld-link /usr/bin/lld-link-16 10
RUN groupadd -g 1001 jenkins
RUN useradd -m -s /bin/bash -g 1001 -u 1001 jenkins
