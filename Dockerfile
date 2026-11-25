# syntax=docker/dockerfile:1
FROM --platform=linux/amd64 ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        make \
        git \
        curl \
        ca-certificates \
        xz-utils \
        tar \
        python3 \
        python3-pip \
        file \
        findutils \
        coreutils \
        gawk \
        sed \
        libc6:i386 \
        libstdc++6:i386 \
        libgcc-s1:i386 \
        zlib1g:i386 \
    && rm -rf /var/lib/apt/lists/*

# JieLi Linux pi32v2 LLVM/Clang Toolchain -> /opt/jieli
RUN curl -fL "https://pkgman.jieliapp.com/s/linux-toolchain" -o /tmp/jieli-toolchain.tar.xz \
    && mkdir -p /opt/jieli \
    && tar -xJf /tmp/jieli-toolchain.tar.xz -C /opt/jieli --strip-components=1 \
    && rm -f /tmp/jieli-toolchain.tar.xz \
    && test -x /opt/jieli/pi32v2/bin/clang \
    && /opt/jieli/pi32v2/bin/clang --version

# JieLi Linux Post-Build-Tools -> /opt/jieli/postbuild
RUN curl -fL "https://pkgman.jieliapp.com/s/linux-postbuild" -o /tmp/jieli-postbuild.tar.xz \
    && mkdir -p /tmp/pb /opt/jieli/postbuild \
    && tar -xJf /tmp/jieli-postbuild.tar.xz -C /tmp/pb \
    && src="$(dirname "$(find /tmp/pb -name isd_download -type f -print -quit)")" \
    && test -n "$src" \
    && cp -a "$src"/. /opt/jieli/postbuild/ \
    && chmod +x /opt/jieli/postbuild/* \
    && rm -rf /tmp/pb /tmp/jieli-postbuild.tar.xz \
    && for t in isd_download ufw_maker fw_add packres json_to_res fat_comm remove_tailing_zeros; do \
         test -x "/opt/jieli/postbuild/$t"; \
       done

ENV PATH="/opt/jieli/pi32v2/bin:/opt/jieli/postbuild:${PATH}"

WORKDIR /workspace
