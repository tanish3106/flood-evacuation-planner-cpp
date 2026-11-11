@echo off
rem Windows (MinGW / MSYS2 g++): compile and start the server. Run from the project folder.
g++ -std=c++17 -O2 -Wall src/main.cpp -o flood_evac.exe -lws2_32
if errorlevel 1 exit /b 1
flood_evac.exe
