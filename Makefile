CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O0
LDFLAGS = -pthread -lssl -lcrypto -lcurl -ljsoncpp

SRC = main.cpp twitch_chat.cpp twitch_auth.cpp twitch_api.cpp cmd_parser.cpp
OBJ = $(SRC:.cpp=.o)

run: $(OBJ)
	$(CXX) $(OBJ) -o run $(LDFLAGS)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
clean:
	rm -f $(OBJ) run