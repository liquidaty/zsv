#!/bin/sh

i=0;
while [ $i -lt 2000 ] ; do
    j=0
    while [ $j -lt 20 ] ; do
        printf '"a,b",'
        j=$(($j+1))
    done

    echo ''
    i=$(($i+1))
done
