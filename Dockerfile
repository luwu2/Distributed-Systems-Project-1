# Use a base image with essential tools
FROM ubuntu:20.04

# Set environment variables to prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install necessary tools and libraries
RUN apt update && apt install -y \
    build-essential \
    g++ \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory inside the container
WORKDIR /app

# Copy source code and hostfile into the container
COPY udp_program.cpp .
COPY hostsfile.txt .

# Compile the program
RUN g++ -o udp_program udp_program.cpp -pthread

# Set the entrypoint to run the compiled program
ENTRYPOINT ["./udp_program"]
