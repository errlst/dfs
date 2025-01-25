#!/bin/bash

# Ensure the script exits if any command fails
set -e

# Get the directory of the current script
script_dir=$(dirname "$0")

# Change to the script directory
cd "$script_dir"

# Iterate over all .proto files in the script directory
for proto_file in *.proto; do
    # Compile the .proto file to .cc and .h files
    protoc --cpp_out=. "$proto_file"
done

echo "All .proto files have been compiled to .cc and .h files."