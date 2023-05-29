#! /bin/bash
# Make the file executable chmod a+x filename
gcc -pthread edge_detector.c
directory="$1"
ppm_files=$(find "$directory" -maxdepth 1 -type f -name "*.ppm")

./a.out $ppm_files