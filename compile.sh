# g++ -std=c++20 main.cpp twitch_api.cpp twitch_auth.cpp -o run -pthread -lssl -lcrypto -lcurl -ljsoncpp
make -j$(nproc)