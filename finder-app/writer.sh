#!/bin/bash
# Check if number of arguments is less than 2
if [ $# -lt 2 ]; then
    echo "Paramater Not fulfilled"
    exit 1
fi

# Creating the file and writing the string
mkdir -p "$(dirname "$1")"
echo $2 >$1
# print error if any
if [ $? -ne 0 ]; then
    echo "Error occurred while creating or writing to file"
    exit 1
fi
exit 0
