#include <iostream>
#include <src/Client.h>
#include <thread>
#include <mutex>
#include <cxxopts.hpp>

int main(int argc, char *argv[]) {
    cxxopts::Options options("P2PC", "A peer-to-peer chat client.");
    options.add_options()
            ("d,debug", "Enable debugging outputs", cxxopts::value<bool>()->default_value("false"))
            ("m,multicastPort", "Multicast Port", cxxopts::value<int>()->default_value(std::to_string(MULTICAST_PORT)))
            ("p,peerPort", "Peer Port", cxxopts::value<int>()->default_value(std::to_string(PEER_PORT)))
            ("n,nickname", "Custom nickname", cxxopts::value<std::string>())
            ("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(EXIT_SUCCESS);
    }

    int multicastPort = result["m"].as<int>();
    if (multicastPort <= 0 || multicastPort > 65535) {
        std::cout << "Invalid Multicast Port passed." << std::endl;
        exit(EXIT_FAILURE);
    }

    int peerPort = result["p"].as<int>();
    if (peerPort <= 0 || peerPort > 65535) {
        std::cout << "Invalid Peer Port passed." << std::endl;
        exit(EXIT_FAILURE);
    }

    std::string nickname;
    if (result.count("n")) {
        nickname = result["n"].as<std::string>();
        if (!NicknameManager::checkNickname(nickname)) {
            std::cout << "Invalid nickname passed. Maximum of 9 chars longs and must consist of letters or numbers"
                      << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    Client client(result["d"].as<bool>(), multicastPort, peerPort, nickname);
    std::mutex consoleMutex;

    // thread to process the input
    std::thread inputThread{[&] {
        std::string input;
        while (true) {
            // Hacky workaround to avoid cout while reading input.
            // Waiting for newline to pause output and wait for input
            getline(std::cin, input);

            std::lock_guard<std::mutex> lockGuard(consoleMutex);
            std::cout << "> ";
            getline(std::cin, input);
            client.pushCommand(input);
        }
    }};

    // thread to process the output
    std::thread outputThread{[&] {
        while (true) {
            while (client.hasOutput()) {
                std::lock_guard<std::mutex> lockGuard(consoleMutex);
                std::cout << client.popOutputMessage() << std::endl;
            }
        }
    }};

    client.start();
    return 0;
}
