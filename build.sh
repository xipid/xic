#!/bin/bash
mkdir -p build
cd build
cmake .. -GNinja
ninja
