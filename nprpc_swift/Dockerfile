# Use official Swift Docker image (Ubuntu-based)
FROM swift:6.2.3

# Install additional build dependencies (Swift image already has build essentials)
RUN apt-get update && apt-get install -y \
    cmake \
    libssl-dev \
    pkg-config \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Create user with same UID/GID as host user
ARG USER_ID=1000
ARG GROUP_ID=1000
ARG USERNAME=builder

RUN (getent group ${GROUP_ID} || groupadd -g ${GROUP_ID} ${USERNAME}) && \
    (getent passwd ${USER_ID} || useradd -u ${USER_ID} -g ${GROUP_ID} -m -s /bin/bash ${USERNAME}) && \
    echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Verify Swift installation
RUN swift --version

# Set working directory
WORKDIR /workspace

# Switch to non-root user (use UID to avoid username issues)
USER ${USER_ID}

# Default command
CMD ["/bin/bash"]
