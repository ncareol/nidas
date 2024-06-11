#!/bin/bash

# Script for testing the processing of sonic anemometer data.

echo "Starting sonic tests..."

source ../nidas_tests.sh
check_executable data_dump

tmperr=$(mktemp /tmp/sonic_test_XXXXXX)
tmpout=$(mktemp /tmp/sonic_test_XXXXXX)
tmpout1=$(mktemp /tmp/sonic_test_XXXXXX)
tmpout2=$(mktemp /tmp/sonic_test_XXXXXX)
awkcom=$(mktemp /tmp/sonic_test_XXXXXX)
trap "{ rm $f $tmpout $tmperr $tmpout1 $tmpout2 $awkcom; }" EXIT

cat << "EOD" > $awkcom
# BEGIN { CONVFMT="%.4g" }
/^2.*/{ 
    for (i = 1; i < 6 && i < NF; i++) {
        printf("%s ",$i)
    }
    for ( ; i < NF; i++) {
        n = $i
        printf("%.3g ",n)
    }
    printf("\n")
}
EOD

error_exit() {
    cat $tmperr 1>&2
    exit 1
}

diff_warn() {
    echo "$1" 1>&2
    cat $2 1>&2

    echo "Repeating diff with fewer significant digits"

    zcat $3 | awk -f $awkcom > $tmpout1
    cat $4 | awk -f $awkcom > $tmpout2

    if ! diff -w $tmpout1 $tmpout2; then
        echo "Second diff failed also"
        exit 1
    fi
    echo "Second diff succeeded"

    # Uncomment to create a "truth" file.
    # [ -f $3 ] || cat $4 | gzip -c > $3
}

diff_exit() {
    echo "$1" 1>&2
    cat $2 1>&2

    # Uncomment to create a "truth" file.
    # [ -f $3 ] || cat $4 | gzip -c > $3
    exit 1
}

test_csat3() {
    export CSAT3_SHADOW_FACTOR=$1
    export CSAT3_ORIENTATION=$2
    export WIND3D_TILT_CORRECTION=$3
    export WIND3D_HORIZ_ROTATION=$4
    local compare_to=$5
    local out=`basename "$compare_to" .gz`
    local msg="shadow=$1, orient=$2, tilt=$3, rotate=$4, output=$out"
    local data_file=data/centnet_20120601_000000.dat.bz2
    echo "Testing CSAT3: $msg"
    data_dump -l 7 -i 6,11 -p -x config/test.xml \
        $data_file 2> "${out}.log" > $out
    # cat $tmperr
    cp -fp "$out" "$tmpout"
    gunzip -c $compare_to | diff -w - $out > $tmperr || diff_warn "WARNING: differences in CSAT3 test of $msg, diff=" $tmperr $compare_to $tmpout
    echo "Test successful"
}

export ATIK_SHADOW_FACTOR=0
export ATIK_SHADOW_ANGLE=70
export ATIK_ORIENTATION=normal

# "Truth" files were created with the data_dump program.
# They have not been verified otherwise, so this test is
# primarily a check that results don't change.

#          shadow orient   tilt  rotate truth-file
test_csat3 0.00 normal     false false data/no_cors.txt.gz
test_csat3 0.00 normal     false true  data/horiz_rot.txt.gz
test_csat3 0.00 normal     true  true  data/tilt_cor.txt.gz
test_csat3 0.16 normal     true  true  data/shadow_cor.txt.gz
test_csat3 0.16 normal     false false data/shadow_cor_only.txt.gz
test_csat3 0.00 down       false false data/down.txt.gz
test_csat3 0.00 flipped    false false data/flipped.txt.gz
test_csat3 0.00 horizontal false false data/horizontal.txt.gz
test_csat3 0.16 horizontal true  true  data/horizontal_all_cors.txt.gz

test_csi_irga() {
    export CSAT3_SHADOW_FACTOR=$1
    export CSAT3_ORIENTATION=$2
    export WIND3D_TILT_CORRECTION=$3
    export WIND3D_HORIZ_ROTATION=$4
    local compare_to=$5
    local msg="shadow=$1, orient=$2, tilt=$3, rotate=$4"
    echo "Testing CSI_IRGA: $msg"
    local data_file=data/centnet_20151104_120000.dat.bz2 
    data_dump -l 6 -i 1,41 -p -x config/test.xml \
        $data_file 2> $tmperr > $tmpout || error_exit

    gunzip -c $compare_to | diff -w - $tmpout > $tmperr || diff_warn "WARNING: differences in CSI_IRGA test of $msg, diff=" $tmperr $compare_to $tmpout
    echo "Test successful"
}

#             shadow orient    tilt  rotate truth-file
test_csi_irga 0.00 normal      false false data/csi_irga_no_cors.txt.gz
test_csi_irga 0.00 normal      false true  data/csi_irga_horiz_rot.txt.gz
test_csi_irga 0.00 normal      true  true  data/csi_irga_tilt_cor.txt.gz
# shadow correction not supported yet for CSI_IRGA
test_csi_irga 0.16 normal      true  true  data/csi_irga_shadow_cor.txt.gz
test_csi_irga 0.16 normal      false false data/csi_irga_shadow_cor_only.txt.gz

test_atik() {
    export ATIK_SHADOW_FACTOR=$1
    export ATIK_SHADOW_ANGLE=70
    export ATIK_ORIENTATION=$2
    export WIND3D_TILT_CORRECTION=$3
    export WIND3D_HORIZ_ROTATION=$4
    local compare_to=$5
    local msg="shadow=$1, orient=$2, tilt=$3, rotate=$4"
    local data_file=data/centnet_20120601_000000.dat.bz2
    echo "Testing ATIK: $msg"
    data_dump -l 6 -i 6,81 -p -x config/test.xml \
        $data_file 2> $tmperr > $tmpout || error_exit

    gunzip -c $compare_to | diff -w - $tmpout > $tmperr || diff_warn "WARNING: differences in ATIK test of $msg, diff=" $tmperr $compare_to $tmpout
    echo "Test successful"
}

#          shadow orient   tilt  rotate truth-file
test_atik 0.00 normal     false false data/atik_no_cors.txt.gz
test_atik 0.00 normal     false true  data/atik_horiz_rot.txt.gz
test_atik 0.00 normal     true  true  data/atik_tilt_cor.txt.gz
test_atik 0.16 normal     true  true  data/atik_shadow_cor.txt.gz
test_atik 0.16 normal     false false data/atik_shadow_cor_only.txt.gz
test_atik 0.00 flipped    false false data/atik_flipped.txt.gz
test_atik 0.16 flipped    true  true  data/atik_flipped_all_cors.txt.gz

echo "Sonic tests succeeded"
exit 0
