#!/bin/bash
# Check if number of arguments is less than 2
if [ $# -lt 2 ]; then
    echo "Paramater Not fulfilled"
    exit 1
fi
# Validate if dir given in first parameter exists
if [ ! -d $1 ]; then
    echo "Directory Not Found"
    exit 1
fi
# counting totoal number of files in given directory
numfiles=$(find $1 -type f | wc -l)

# CHECKING FOR THE  string in files
match=0
for line in $(grep -rc $2 $1); do
    count=$(echo $line | awk -F: '{print $2}')
    match=$((match + count))
done
echo "The number of files are $numfiles and the number of matching lines are $match"
