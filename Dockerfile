FROM ubuntu:22.04

RUN sed -i 's/archive.ubuntu.com/mirrors.kernel.org/g' /etc/apt/sources.list && \ 
    sed -i 's/security.ubuntu.com/mirrors.kernel.org/g' /etc/apt/sources.list

RUN apt update && yes | unminimize

RUN apt update && apt install -y \
    build-essential \
    bat \
    curl \
    file \
    gdb \
    git \
    htop \
    man \
    python3 \
    screen \
    tmux \
    valgrind \
    wget \
    xxd \ 
    netcat \
    zip

# Ubuntu 22.04 default clang-format package is too old
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-19 main" >> \
    /etc/apt/sources.list
RUN echo "deb-src http://apt.llvm.org/jammy/ llvm-toolchain-jammy-19 main" >> \
    /etc/apt/sources.list

RUN apt update && apt install -y clang-format-19
RUN ln -s /usr/bin/clang-format-19 /usr/bin/clang-format


RUN mkdir -p /root/.config/bat/
RUN touch /root/.config/bat/config
RUN echo "--theme=GitHub" > /root/.config/bat/config
