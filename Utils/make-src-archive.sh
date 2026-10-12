#!/bin/bash
set -e

test -e ProductInfo.h && cd ..
if ! test -d src; then
	echo Run this script from the top directory.
	exit 1
fi

source Utils/GetProductVer.sh src/ProductInfo.h
PRODUCTVER="$PRODUCTVER-src"

if test -e $PRODUCTVER.tar.gz; then
	echo "\"$PRODUCTVER.tar.gz\" already exists."
	exit 1
fi

echo Archiving...
tar zcvf "$PRODUCTVER".tar.gz \
	--transform "s,^\.,$PRODUCTVER," \
	--exclude='./build*' \
	--exclude='./*.tar.gz' \
	--exclude='*.o' \
	--exclude='*.a' \
	.
