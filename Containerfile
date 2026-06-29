FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    # SSH
    openssh-server \
    openssh-client \
    # Shell & outils de base
    bash \
    coreutils \
    util-linux \
    procps \
    less \
    vim-tiny \
    htop \
    man-db \
    # Réseau — diagnostic
    iproute2 \
    net-tools \
    iputils-ping \
    iputils-tracepath \
    traceroute \
    tcpdump \
    nmap \
    netcat-openbsd \
    dnsutils \
    mtr-tiny \
    iperf3 \
    # Réseau — contrôle
    iptables \
    isc-dhcp-client \
    # Transfert & archives
    curl \
    wget \
    tar \
    gzip \
    rsync \
    # Compilation & debug
    gcc \
    make \
    strace \
    # Divers
    sudo \
    tree \
    jq \
 && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash player && \
    echo 'player:datacenter2031' | chpasswd && \
    echo 'player ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/player && \
    chmod 440 /etc/sudoers.d/player

RUN ssh-keygen -A && \
    echo 'PasswordAuthentication yes' >> /etc/ssh/sshd_config && \
    echo 'PermitRootLogin no'         >> /etc/ssh/sshd_config

EXPOSE 22
CMD ["/bin/sh", "-c", "mkdir -p /run/sshd && exec /usr/sbin/sshd -D"]
