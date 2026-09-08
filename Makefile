CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O0 -I/usr/include/jsoncpp $(shell pkg-config --cflags lua5.4)
LDFLAGS = -pthread -lssl -lcrypto -lcurl -ljsoncpp $(shell pkg-config --libs lua5.4)

SRC = main.cpp twitch_chat.cpp twitch_auth.cpp twitch_api.cpp cmd_parser.cpp
OBJ = $(SRC:.cpp=.o)

run: $(OBJ)
	$(CXX) $(OBJ) -o run $(LDFLAGS)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
clean:
	rm -f $(OBJ) run
