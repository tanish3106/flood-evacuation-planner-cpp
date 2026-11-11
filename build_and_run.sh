#!/bin/sh
# Linux / macOS: compile and start the server (run from the project folder)
g++ -std=c++17 -O2 -Wall src/main.cpp -o flood_evac && ./flood_evac
