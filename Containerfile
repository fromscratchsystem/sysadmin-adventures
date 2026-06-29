FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    openssh-server \
    bash \
    coreutils \
    util-linux \
    procps \
    less \
    vim-tiny \
    htop \
    iproute2 \
    net-tools \
    iputils-ping \
    curl \
    wget \
    openssh-client \
    tar \
    gzip \
    sudo

RUN useradd -m -s /bin/bash player && \
    echo 'player:datacenter2031' | chpasswd && \
    echo 'player ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/player && \
    chmod 440 /etc/sudoers.d/player

RUN ssh-keygen -A && \
    echo 'PasswordAuthentication yes' >> /etc/ssh/sshd_config && \
    echo 'PermitRootLogin no'         >> /etc/ssh/sshd_config

EXPOSE 22
CMD ["/bin/sh", "-c", "mkdir -p /run/sshd && exec /usr/sbin/sshd -D"]
