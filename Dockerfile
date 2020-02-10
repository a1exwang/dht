FROM alpine:3 AS base
RUN apk --update add tar gzip openssl sqlite-libs curl && \
    rm -rf /var/lib/apt/lists/* && \
    rm /var/cache/apk/*

FROM base AS build
RUN apk add boost-dev cmake gcc g++ openssl-dev make sqlite-dev
RUN apk add boost-static

RUN mkdir /build
ADD 3rdparty /build/3rdparty
ADD include /build/include
ADD src /build/src
ADD programs /build/programs
ADD CMakeLists.txt /build
RUN cd /build && mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/albert ..
RUN cd /build/build && make -j$(nproc) && make install

FROM base AS runtime
RUN apk add gdb openvpn
COPY --from=build /usr/local/albert /usr/local/albert
ENV PATH=${PATH}:/usr/local/albert/bin
