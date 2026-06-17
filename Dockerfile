ARG UBUNTU_VERSION=24.04

FROM ubuntu:${UBUNTU_VERSION} AS base

# Set non-interactive mode for apt-get
ENV DEBIAN_FRONTEND=noninteractive

# Install common dependencies
RUN apt-get update \
    && apt-get install -y -q --no-install-recommends \
    apt-utils \
    bash-completion \
    build-essential \
    ca-certificates \
    curl \
    git \
    gnupg2 \
    wget \
    locales \
    lsb-release \
    mesa-utils \
    ninja-build \
    openssh-client \
    software-properties-common \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Set locales to en_US.UTF-8
RUN locale-gen en_US.UTF-8 \
    && update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# Set environment variables for locale
ENV LANG=en_US.UTF-8 \
    LANGUAGE=en_US:en \
    LC_ALL=en_US.UTF-8

# GCC 13 — required for C++20 <format> header support.
RUN apt-get update && apt-get install -y --no-install-recommends gcc-13 g++-13 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 130 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-13 \
    --slave /usr/bin/gcov gcov /usr/bin/gcov-13 \
    && rm -rf /var/lib/apt/lists/*

# Install CMake
ARG CMAKE_VERSION=3.31.9
RUN wget https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh \
    && chmod +x cmake-${CMAKE_VERSION}-linux-x86_64.sh \
    && ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --skip-license --prefix=/usr/local \
    && rm cmake-${CMAKE_VERSION}-linux-x86_64.sh

FROM base AS dev

# Install LLVM tools
ARG LLVM_VERSION=20
RUN wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
    | gpg --dearmor -o /usr/share/keyrings/llvm-snapshot.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.gpg] http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-${LLVM_VERSION} main" \
    > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y -q --no-install-recommends \
    clang-format-${LLVM_VERSION} \
    clang-tidy-${LLVM_VERSION} \
    sudo \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-${LLVM_VERSION} 1 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-${LLVM_VERSION} 1

# GL + X11 for the Polyscope demos (build + runtime); harmless if demos are off.
RUN apt-get update \
    && apt-get install -y -q --no-install-recommends \
    xorg-dev \
    libgl1-mesa-dev \
    libgl1-mesa-dri \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install CCache
ARG CCACHE_VERSION=4.11.3
RUN git clone --branch v${CCACHE_VERSION} https://github.com/ccache/ccache.git /opt/ccache \
    && mkdir -p /opt/ccache/build && cd /opt/ccache/build \
    && cmake \
    -DCMAKE_BUILD_TYPE=Release \
    .. \
    && make install -j$(nproc --ignore=1) \
    && rm -rf /opt/ccache/build /opt/ccache/.git

# Create a non-root user to use if preferred
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=$USER_UID
RUN if id -u ${USER_UID} >/dev/null 2>&1; then \
        userdel -r "$(getent passwd ${USER_UID} | cut -d: -f1)" 2>/dev/null || true; \
    fi \
    && if getent group ${USER_GID} >/dev/null; then \
        groupdel "$(getent group ${USER_GID} | cut -d: -f1)"; \
    fi \
    && groupadd --gid ${USER_GID} ${USERNAME} \
    && useradd -s /bin/bash --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME} \
    && echo ${USERNAME} ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/${USERNAME} \
    && chown -R ${USERNAME}:${USERNAME} /home/${USERNAME}
USER ${USERNAME}

# Set working directory
ENV HOME=/home/${USERNAME}
ARG WORKSPACE=workspaces
WORKDIR ${HOME}/${WORKSPACE}

# Default command
CMD [ "sleep", "infinity" ]
