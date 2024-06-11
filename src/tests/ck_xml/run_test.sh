#!/bin/bash

# Test script which runs ck_xml on a bunch of project configurations.

cwd=`dirname $0`
echo "cwd=$cwd"

source ../nidas_tests.sh

check_executable ck_xml
check_executable datasets

valgrind=""

if [ "$1" == "--valgrind" ]; then
    valgrind="valgrind --suppressions=suppressions.txt --leak-check=full --gen-suppressions=all"
fi


check_valgrind()
{
    # check for valgrind errors
    xml_errs=`valgrind_errors tmp/ck_xml.log`
    echo "$xml_errs errors reported by valgrind in tmp/ck_xml.log"

    if [ $xml_errs -ne 0 ]; then
        echo "ck_xml test failed"
        exit 1
    fi
}


[ -d $cwd/tmp ] && rm -rf $cwd/tmp
[ -d $cwd/tmp ] || mkdir $cwd/tmp

set -o pipefail

cp $cwd/../../xml/nidas.xsd $cwd/xml
mkdir -p output


run_ck_xml() # cmd output
{
    # run the command once to get output, and again for valgrind output
    cmd="$1"
    output="$2"
    echo "$cmd > $output"
    $cmd > $output 2>&1
    if [ $? -ne 0 ]; then
        echo Failed.
        tail $output
        exit 1
    fi
    sed -i -e '/deprecated/d' $output

    diff baseline/`basename $output` $output || exit 1

    if [ -n "$valgrind" ]; then
        echo "$valgrind $cmd"
        $valgrind $cmd 2>&1 >/dev/null | tee tmp/ck_xml.log || exit 1
        test -n "$valgrind" && check_valgrind
    fi
}


find -L xml -name "*.xml" | grep -vE "sensor_catalog|datasets|stats" | \
while read xml ; do

    xmldir=`dirname "$xml"`
    if [ -f "$xmldir/datasets.xml" ]; then
        eval `datasets "$xmldir/datasets.xml" -b noqc_geo`
    fi

    # for each xml example, generate the default ck_xml output and also the
    # variables list.
    outbase="output/`basename $xml .xml`"
    run_ck_xml "ck_xml $xml" "${outbase}.default.txt"
    run_ck_xml "ck_xml --variables $xml" "${outbase}.variables.txt"

done


