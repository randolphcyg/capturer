# 第一阶段：构建阶段
FROM golang:1.24.0 AS builder

LABEL stage=gobuilder

ENV CGO_ENABLED=1
ENV GOPROXY=https://goproxy.cn,direct
ENV PKG_CONFIG_PATH=/usr/local/lib/x86_64-linux-gnu/pkgconfig
ENV C_INCLUDE_PATH=/usr/local/include/dpdk

# 安装最小依赖（编译 DPDK 所需）
RUN apt-get update && \
    apt-get install -y \
    build-essential \
    clang \
    libpcap-dev \
    libnuma-dev \
    libglib2.0-dev \
    libatomic1 \
    pkg-config \
    meson \
    ninja-build \
    wget \
    python3-pip && \
    python3 -m pip install --break-system-packages pyelftools && \
    rm -rf /var/lib/apt/lists/*


# 下载并编译 DPDK
RUN wget https://fast.dpdk.org/rel/dpdk-24.11.tar.xz && \
    mkdir dpdk && \
    tar -xf dpdk-24.11.tar.xz --strip-components=1 -C dpdk && \
    cd dpdk && \
    meson setup build && \
    meson configure build && \
    ninja -C build && \
    ninja -C build install && \
    cd .. && \
    rm -rf dpdk dpdk-24.11.tar.xz

# 确保 pkg-config 能找到 dpdk.pc
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib/pkgconfig

WORKDIR /capturer

# 复制 Go 依赖文件并下载依赖
COPY go.mod .
COPY go.sum .
RUN go mod download

# 复制全部代码
COPY . .

# 编译 Go + CGO 代码
RUN go build -ldflags="-s -w" -o /capturer/capturer ./cmd/main.go

# 第二阶段：运行时环境
FROM ubuntu:22.04

ENV TZ=Asia/Shanghai
ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV LANGUAGE=C.UTF-8
ENV LC_ALL=C.UTF-8

# 安装运行时所需的最小依赖
RUN apt-get update && \
    apt-get install -y \
    libpcap-dev \
    libnuma1 \
    libglib2.0-0 \
    libatomic1 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 复制编译后的二进制文件
COPY --from=builder /capturer/capturer /app/capturer

# 复制 DPDK 运行时库
COPY --from=builder /usr/local/lib/x86_64-linux-gnu /usr/local/lib/x86_64-linux-gnu
COPY --from=builder /usr/local/share/dpdk /usr/local/include/dpdk

# 设置动态链接库路径
ENV LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu

# 复制 CA 证书和时区信息
COPY --from=builder /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
COPY --from=builder /usr/share/zoneinfo/Asia/Shanghai /usr/share/zoneinfo/Asia/Shanghai


# 启动抓包服务
ENTRYPOINT ["/app/capturer"]