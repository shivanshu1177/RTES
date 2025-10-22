# Multi-stage build for RTES Exchange
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libgtest-dev \
    pkg-config \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /src

# Copy source code
COPY . .

# Build the project
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=x86-64" && \
    cmake --build build -j$(nproc) && \
    cmake --install build --prefix /opt/rtes

# Runtime stage
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd -r rtes && useradd -r -g rtes rtes

# Copy built binaries and configs
COPY --from=builder /opt/rtes /opt/rtes
COPY --from=builder /src/configs /opt/rtes/etc/rtes/
COPY --from=builder /src/tools/*.py /opt/rtes/bin/

# Set up directories and permissions
RUN mkdir -p /var/log/rtes /var/lib/rtes && \
    chown -R rtes:rtes /var/log/rtes /var/lib/rtes /opt/rtes

# Add binaries to PATH
ENV PATH="/opt/rtes/bin:${PATH}"

# Switch to non-root user
USER rtes

# Expose ports
EXPOSE 8888 8080 9999/udp

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Default command
ENTRYPOINT ["trading_exchange"]
CMD ["/opt/rtes/etc/rtes/config.json"]