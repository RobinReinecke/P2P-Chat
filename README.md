# P2P Chat
A Peer-to-Peer Chat written in C++ for my computer science studies. The specialty of this implementation is the NetworkManager, which is fully relying on POSIX. This means it does not need any networking library like Boost.
Further every network communication is RSA e2e-encrypted. Personal and group messages are AES encryted.
Only external dependencies are [nlohmann_json](https://github.com/nlohmann/json) and [cxxopts](https://github.com/jarro2783/cxxopts) to simplify the remaining implementation.

### Install Dependencies
The graphviz library is used to generate a png of the current network topology.
```
apt install graphviz libgraphviz-dev
```

### Compile
cmake automatically installs the dependencies (except graphviz).
```
cd build
cmake ..
make
```

### Run
```
./client [-h/--help] [-n/--nickname NAME] [-d/--debug] [-m/--multicastPort XXXXX] [-p/--peerPort XXXXX]
```

## Software Architecture
The Client is split into several modules (*\*Manager.(cpp|h)*). The main class is in *Client.cpp* which combines all modules. In the *main.cpp* three threads are created. Two for I/O and one for the Clients infinite loop. This loop checks for new messages and processes the input.