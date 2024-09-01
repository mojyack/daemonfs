#!/bin/zsh

trap 'echo term!' TERM
echo "trapped"

count=1
while true; do
    echo "hello!" $count
    count=$(($count+1))
    sleep 1
done
