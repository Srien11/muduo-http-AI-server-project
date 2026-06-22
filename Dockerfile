FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y     build-essential cmake     libssl-dev     libcurl4-openssl-dev     libmysqlclient-dev     wget git     && rm -rf /var/lib/apt/lists/*

# Build and install Muduo from source (master branch)
WORKDIR /tmp
RUN git clone --depth 1 https://github.com/chenshuo/muduo.git &&     cd muduo &&     cmake -S . -B build &&     cmake --build build -j16 &&     cmake --install build &&     ldconfig

WORKDIR /app
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/
COPY www/ ./www/
COPY certs/ ./certs/
COPY server.conf.example ./server.conf

RUN cmake -S . -B build &&     cmake --build build -j16

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y     libssl3t64     libcurl4t64     ca-certificates     && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/libmuduo* /usr/local/lib/
RUN ldconfig

WORKDIR /app
COPY --from=builder /app/build/muduo_http_server .
COPY --from=builder /app/www/ ./www/
COPY --from=builder /app/certs/ ./certs/
COPY --from=builder /app/server.conf .

EXPOSE 8080

CMD ["./muduo_http_server"]
