CXX := g++
CXXFLAGS := -O2 -std=c++17 -pipe -Wall -Wextra
TARGET := sta
SRCS := parser.cpp example_main.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
    $(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
    rm -f $(TARGET) *.o *~ *_parsed.json
