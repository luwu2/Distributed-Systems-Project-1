#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#define PORT 5000
#define BUFFER_SIZE 1024
#define MAX_RETRIES 5
#define RETRY_DELAY_MS 1000

// Function to parse the hostfile and return a vector of peer hostnames
std::vector<std::string> parse_hostfile(const std::string& filename, const std::string& my_hostname) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Failed to open hostfile: " << filename <<"\n";
        exit(EXIT_FAILURE);
    }

    std::vector<std::string> peers;
    std::string line;
    while (std::getline(file, line)) {
        if (line != my_hostname) {
            peers.push_back(line);
        }
    }
    return peers;
}

// Function to send readiness messages to all peers
void send_ready_messages(const std::vector<std::string>& peers, const std::string& my_hostname) {
    // Create a socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        for (const auto& peer : peers) {
            sockaddr_in peer_addr{};
            peer_addr.sin_family = AF_INET;
            peer_addr.sin_port = htons(PORT);

            // Initialize getaddrinfo hints
            struct addrinfo hints = {};
            hints.ai_family = AF_INET;  // Only look for IPv4 addresses
            hints.ai_socktype = SOCK_DGRAM;  // Datagram socket for UDP

            struct addrinfo* res;
            int err = getaddrinfo(peer.c_str(), nullptr, &hints, &res);
            if (err != 0) {
                std::cerr << "Error resolving hostname: " << peer << " (" << gai_strerror(err) << ")\n";
                continue;  // Skip this peer and try the next one
            }

            if (res == nullptr) {
                std::cerr << "No addresses found for " << peer << "\n";
                continue;  // Skip this peer and try the next one
            }

            // Extract the first resolved address and cast it to sockaddr_in
            peer_addr.sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
            freeaddrinfo(res);  // free addrinfo

            // Construct the message to send
            std::string message = my_hostname + " READY";

            // Send message using the correct address structure
            ssize_t bytes_sent = sendto(sock, message.c_str(), message.size(), 0, (const sockaddr*)&peer_addr, sizeof(peer_addr));
            if (bytes_sent <= 0) {
                std::cerr << "sendto failed: " << strerror(errno) << std::endl;
            }
        }

        // Wait before retrying to give peers time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
    }

    close(sock);  // Close the socket when done
}

// Function to receive messages
void receive_messages(std::unordered_set<std::string>& ready_peers, size_t total_peers) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    sockaddr_in my_addr{};
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (const sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
        perror("Bind failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE];
    while (ready_peers.size() < total_peers) {
        sockaddr_in sender_addr{};
        socklen_t addr_len = sizeof(sender_addr);
        ssize_t bytes_received = recvfrom(sock, buffer, BUFFER_SIZE, 0, (sockaddr*)&sender_addr, &addr_len);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string message(buffer);

            // Log the sender hostname and message for debugging
            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));

            // Extract hostname and add to ready_peers
            size_t pos = message.find(" READY");
            if (pos != std::string::npos) {
                std::string sender_hostname = message.substr(0, pos);
                ready_peers.insert(sender_hostname);
            }
        } else if (bytes_received < 0) {
            std::cerr << "Failed to receive message: " << strerror(errno) << std::endl;
        }
    }
    close(sock);
}

int main(int argc, char* argv[]) {
    // Validate command-line arguments
    if (argc != 3 || std::strcmp(argv[1], "-h") != 0) {
        std::cerr << "Usage: " << argv[0] << " -h <hostfile>\n";
        return EXIT_FAILURE;
    }

    const std::string hostfile = argv[2];

    // Get the container's hostname using the Docker DNS mechanism
    char hostname[BUFFER_SIZE];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("Failed to get hostname");
        return EXIT_FAILURE;
    }
    std::string my_hostname(hostname);

    // Parse hostfile to identify peer hostnames
    std::vector<std::string> peers = parse_hostfile(hostfile, my_hostname);
    if (peers.empty()) {
        std::cerr << "No peers found in the hostfile.\n";
        return EXIT_FAILURE;
    }
    size_t total_peers = peers.size();

    // Set of ready peers
    std::unordered_set<std::string> ready_peers;

    // Start receiver in a separate thread
    std::thread receiver(receive_messages, std::ref(ready_peers), total_peers);

    // Send readiness messages
    send_ready_messages(peers, my_hostname);

    // Wait for receiver thread to complete
    receiver.join();

    // Output "READY" once all peers have responded
    std::cerr << "READY\n";
    return EXIT_SUCCESS;
}
