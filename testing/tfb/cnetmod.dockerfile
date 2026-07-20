FROM ubuntu:24.04

# Install build tools and dependencies
RUN apt-get update -yqq && \
    apt-get install -yqq --no-install-recommends \
    software-properties-common sudo curl wget git ca-certificates \
    lsb-release gnupg \
    openssl libssl-dev zlib1g-dev liburing-dev && \
    rm -rf /var/lib/apt/lists/*

# Install LLVM/Clang 21 with libc++ (required for C++23 modules)
RUN wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && \
    ./llvm.sh 21 all && \
    apt-get install -yqq --no-install-recommends \
    libc++-21-dev libc++abi-21-dev && \
    rm -f llvm.sh && \
    rm -rf /var/lib/apt/lists/*

# Install CMake 4.x (required for C++23 module support)
RUN wget -qO- https://github.com/Kitware/CMake/releases/download/v4.0.0/cmake-4.0.0-linux-x86_64.tar.gz | \
    tar xz -C /opt && \
    ln -s /opt/cmake-4.0.0-linux-x86_64/bin/cmake /usr/local/bin/cmake && \
    ln -s /opt/cmake-4.0.0-linux-x86_64/bin/ctest /usr/local/bin/ctest

# Install Ninja
RUN apt-get update -yqq && \
    apt-get install -yqq --no-install-recommends ninja-build && \
    rm -rf /var/lib/apt/lists/*

ENV CC=clang-21
ENV CXX=clang++-21

COPY ./ /cnetmod

WORKDIR /cnetmod

# Initialize submodules
RUN git submodule update --init --recursive || true

# Build release
RUN cmake -B build-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG -flto" \
    -DCNETMOD_BUILD_EXAMPLES=ON \
    -DCNETMOD_BUILD_TESTS=OFF \
    -DCNETMOD_BUILD_BENCH=OFF && \
    cmake --build build-release --target example_tfb_benchmark -j$(nproc)

EXPOSE 8080

ENV DBHOST=tfb-database
ENV DBPORT=3306
ENV DBUSER=benchmarkdbuser
ENV DBPASS=benchmarkdbpass
ENV DBNAME=hello_world

CMD ["./build-release/examples/example_tfb_benchmark"]
