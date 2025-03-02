FROM --platform=linux/amd64 debian:bookworm

# Install build tools, kernel headers, etc.
RUN apt-get update && \
    apt-get install -y build-essential cmake gdb clang git && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Create a non-root user (optional but recommended)
RUN useradd -ms /bin/bash developer
USER developer
WORKDIR /home/developer

# Optional: Copy your source code into the container
COPY . /home/developer

# Optional: Compile within the container
#RUN gcc -o fancontrol fancontrol.cpp
