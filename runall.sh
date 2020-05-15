#!/bin/bash
# Read a string with spaces using for loop
cd build

#https://crunchify.com/shell-script-append-timestamp-to-file-name/
current_time=$(date "+%Y.%m.%d-%H.%M.%S")
echo "Current Time : $current_time"

new_fileName=log.$current_time.txt
echo "New FileName: " "$new_fileName"

for detector in SHITOMASI HARRIS FAST BRISK ORB SIFT
do
    for descriptor in BRISK BRIEF ORB FREAK SIFT
    do
       echo "Running.. $detector $descriptor"
       echo "#BEGIN $detector $descriptor" >> ../$new_fileName
       ./3D_object_tracking $detector $descriptor >> ../$new_fileName
       echo "#END $detector $descriptor" >> ../$new_fileName
    done
done
for detector in AKAZE
do
    for descriptor in AKAZE
    do
       echo "Running.. $detector $descriptor"
       echo "#BEGIN $detector $descriptor" >> ../$new_fileName
       ./3D_object_tracking $detector $descriptor >> ../$new_fileName
       echo "#END $detector $descriptor" >> ../$new_fileName
    done
done
cd ../