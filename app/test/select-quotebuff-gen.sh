i=0;
while [ $i -lt 2000 ] ; do
    j=0
    while [ $j -lt 20 ] ; do
        printf '"a,b",'
        ((j++))
    done

    echo ''
    ((i++))
done
