#!/bin/bash

clang_path=${1-clang}
bin_path=${2-../../build/bin/callgraph-profiler}
test_path=${3-../c}

for testfile in $test_path/*.c; do
    echo "Verifying test case $testfile"
    bin_name=$(uuidgen)_calls
    $clang_path -g -c -emit-llvm $testfile -o calls.bc
    $bin_path calls.bc -o $bin_name > temphistory
    # python calltester.py $bin_name $testfile
    rm $bin_name
    rm $bin_name.o
    rm $bin_name.callcounter.bc
done
rm calls.bc
rm temphistory
