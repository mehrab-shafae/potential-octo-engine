#!/bin/bash

g++ -std=c++20 -O2 -Wall aspire.cpp -luring -pthread -o aspire_server

./aspire_server
