# TriviaGame

A programming trivia game written in C++ with raylib: main menu, shop, settings, music and effects.

## Build

Requires g++ (C++20) and the [raylib](https://www.raylib.com) library.

```
cd src
g++ -std=c++20 main.cpp -o trivia $(pkg-config --cflags --libs raylib)
```

Version: 1.0.0
