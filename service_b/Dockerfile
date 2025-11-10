# Stage 1: Build Stage
# Use an official GCC image to build the C++ application
FROM gcc:14.2.0 AS build

# Install required libraries and dependencies for building the app
RUN apt-get update && apt-get install -y \
    cmake \
    make \
    wget \
    git \
    libcurlpp-dev \
    libboost-dev \
    libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory for the build process
WORKDIR /app

# Download ASIO and Crow libraries from GitHub
RUN git clone https://github.com/chriskohlhoff/asio.git asio
RUN git clone https://github.com/CrowCpp/Crow.git crow

# Copy your source code (main.cpp) into the container
COPY . .

# Build the application using g++
RUN g++ -Icrow/include -Iasio/asio/include -L/usr/local/lib -o service_b main.cpp -lpthread -lcurlpp -lcurl -lhiredis -lssl -lcrypto

# Stage 2: Runtime Stage
# Use an official GCC image (same as the build stage) for runtime
FROM gcc:14.2.0

# Install only the runtime dependencies needed to run the app
RUN apt-get update && apt-get install -y \
    libcurlpp-dev \
    libboost-dev \
    libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory for the runtime container
WORKDIR /app

# Copy the compiled binary (service_b) from the build stage into the runtime container
COPY --from=build /app/service_b .

# Expose port 8080 for your app (modify this if needed based on your app's settings)
EXPOSE 8080

# Set the default command to run the application
CMD ["./service_b"]
