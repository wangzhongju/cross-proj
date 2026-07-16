#! /bin/bash

# define file patch
FILE_A="$1"
FILE_B="$2"
FILE_C="$3"

echo "FILE_A $FILE_A"
echo "FILE_B $FILE_B"
echo "FILE_C $FILE_C"
OFFSET=1M

dd if="$FILE_A" of="$FILE_C" bs=1 count=$OFFSET

dd if="$FILE_B" of="$FILE_C" bs=1 seek=$OFFSET conv=notrunc status=none
