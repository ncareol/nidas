#!/bin/sh

# Test script for a dsm process, sampling serial sensors, via pseudo-terminals

# If the first runstring argument is "installed", then don't fiddle with PATH or
# LD_LIBRARY_PATH, and run the nidas programs from wherever they are found in PATH.
# Otherwise if build/apps is not found in PATH, prepend it, and if LD_LIBRARY_PATH 
# doesn't contain the string build, prepend ../build/{util,core,dynld}.

# scons may not set HOSTNAME
export HOSTNAME=`hostname`

installed=false
[ $# -gt 0 -a "$1" == "-i" ] && installed=true

if ! $installed; then

    echo $PATH | fgrep -q build/apps || PATH=../../build/apps:$PATH

    llp=../../build/util:../../build/core:../../build/dynld
    echo $LD_LIBRARY_PATH | fgrep -q build || \
        export LD_LIBRARY_PATH=$llp${LD_LIBRARY_PATH:+":$LD_LIBRARY_PATH"}

    echo LD_LIBRARY_PATH=$LD_LIBRARY_PATH
    echo PATH=$PATH

    if ! which dsm | fgrep -q build/; then
        echo "dsm program not found on build directory. PATH=$PATH"
        exit 1
    fi
    if ! ldd `which dsm` | awk '/libnidas/{if (index($0,"build/") == 0) exit 1}'; then
        echo "using nidas libraries from somewhere other than a build directory"
        exit 1
    fi
fi

# echo PATH=$PATH
# echo LD_LIBRARY_PATH=$LD_LIBRARY_PATH

echo "dsm executable: `which dsm`"
echo "nidas libaries:"
ldd `which dsm` | fgrep libnidas

valgrind_errors() {
    egrep -q "^==[0-9]*== ERROR SUMMARY:" $1 && \
        sed -n 's/^==[0-9]*== ERROR SUMMARY: \([0-9]*\).*/\1/p' $1 || echo 1
}

kill_dsm() {
    # send a TERM signal to dsm process
    nkill=0
    dsmpid=`pgrep -f "valgrind .*dsm -d"`
    if [ -n "$dsmpid" ]; then
        echo "Doing kill -TERM $dsmpid"
        kill -TERM $dsmpid
        while ps -p $dsmpid > /dev/null; do
            if [ $nkill -gt 10 ]; then
                echo "Doing kill -9 $dsmpid"
                kill -9 $dsmpid
            fi
            nkill=$(($nkill + 1))
            sleep 1
        done
    fi
}

kill_sims() {
    for pid in ${sspids[*]}; do
        if [ $pid -gt 0 ] && kill -0 $pid >& /dev/null; then
            echo "Killing sensor_sim, pid=$pid"
            kill -9 $pid
        fi
    done
}


find_udp_port() {
    local -a inuse=(`netstat -uan | awk '/^udp/{print $4}' | sed -r 's/.*:([0-9]+)$/\1/' | sort -u`)
    local port1=$(( $(cat /proc/sys/net/ipv4/ip_local_port_range | awk '{print $1}') - 1))
    for (( port = $port1; ; port--)); do
        echo ${inuse[*]} | fgrep -q $port || break
    done
    echo $port
}
        
kill_dsm

for f in /tmp/run/nidas/dsm.pid; do
    if [ -f $f ]; then
        echo "$f exists, deleting"
        rm $f || exit 1
    fi
done

export TEST=$(mktemp -d --tmpdir test_XXXXXX)
echo "TEST=$TEST"

trap "{ rm -rf $TEST; }" EXIT

badexit() {
    save=/tmp/test_save1
    echo "Saving $TEST as $save"
    [ -d $save ] && rm -rf $save
    mv $TEST $save
    exit 1
}


# build the local sensor_sim program
#cd src || exit 1
#scons
#cd ..

# Start sensor simulations on pseudo-terminals.
# Once sensor_sim opens the pseudo-terminal it does a kill -STOP on itself.
# After starting the sensor_sims, this script then starts the dsm process.
# This script scans the dsm process output for an "opening $TEST/testN" message
# indicating that the the dsm process has opened the device. At that point
# do a kill -CONT on the corresponding sensor_sim so it starts sending data
# on the pseudo terminal.
sspids=()
# enable verbose on this first sensor_sim
sensor_sim -f data/test.dat -e "\n" -r 10 -t $TEST/test0 &
sspids=(${sspids[*]} $!)
sensor_sim -f data/test.dat -b $'\e' -r 10 -t $TEST/test1 &
sspids=(${sspids[*]} $!)
# simulate Campbell sonic
sensor_sim -c -r 60 -n 256 $TEST/test2 -t > $TEST/csat3.log 2>&1 &
sspids=(${sspids[*]} $!)
sensor_sim -f data/repeated_sep.dat -e xxy -r 1 -t $TEST/test3 &
sspids=(${sspids[*]} $!)
sensor_sim -f data/repeated_sep.dat -b xxy -r 1 -t $TEST/test4 &
sspids=(${sspids[*]} $!)
sensor_sim -f data/repeated_sep.dat -p "hello\n" -e "\n" -t $TEST/test5 &
sspids=(${sspids[*]} $!)

# number of simulated sensors
nsensors=${#sspids[*]}

rm -f $TEST/dsm.log

export NIDAS_SVC_PORT_UDP=`find_udp_port`
echo "Using port=$NIDAS_SVC_PORT_UDP"

# start dsm data collection
( valgrind --suppressions=suppressions.txt --gen-suppressions=all --leak-check=full dsm -d -l 6 config/test.xml 2>&1 | tee $TEST/dsm.log ) &
dsmpid=$!

while ! [ -f $TEST/dsm.log ]; do
    sleep 1
done

# look for "opening" messages in dsm output
sleep=0
sleepmax=30
ndone=0
while [ $ndone -lt $nsensors -a $sleep -lt $sleepmax ]; do
    for (( n = 0; n < $nsensors; n++ )); do
        if [ ${sspids[$n]} -gt 0 ]; then
            # if fgrep -q "opened: ${HOSTNAME}:$TEST/test$n" $TEST/dsm.log; then
            if fgrep -q "opening: $TEST/test$n" $TEST/dsm.log; then
                echo "sending CONT to ${sspids[$n]} for $TEST/test$n"
                kill -CONT ${sspids[$n]}
                sspids[$n]=-1
                ndone=$(($ndone + 1))
            else
                sleep 1
                sleep=$(($sleep + 1))
            fi
        fi
    done
done

if [ $sleep -ge $sleepmax ]; then
    echo "Cannot find \"opening\" messages in dsm output."
    echo "dsm process is apparently not running successfully."
    echo "${0##*/}: serial_sensor test failed"
    kill_sims
    kill_dsm
    cat $TEST/dsm.log
    badexit
fi


# When a sensor_sim finishes and closes its pseudo-terminal
# the dsm process gets an I/O error reading the pseudo-terminal device.
# The dsm process then closes the device, and schedules it to be re-opened.
# Look for these "closed" messages in the dsm output, which indicate
# that the sensor_sim processes have finished.  We could instead
# check their process ids.
while true; do
    ndone=0
    for (( n = 0; n < $nsensors; n++ )); do
        if fgrep -q "closing: $TEST/test$n" $TEST/dsm.log; then
            ndone=$(($ndone + 1))
        else
            sleep 1
        fi
    done
    [ $ndone -eq $nsensors ] && break
done

kill_dsm

# check output data file for the expected number of samples
ofiles=($TEST/${HOSTNAME}_*)
if [ ${#ofiles[*]} -ne 1 ]; then
    echo "Expected one output file, got ${#ofiles[*]}"
    badexit
fi

# run data_stats on raw data file
statsf=$TEST/data_stats.out
data_stats $ofiles > $statsf

ns=`egrep "^$HOSTNAME:$TEST/test" $statsf | wc | awk '{print $1}'`
if [ $ns -ne $nsensors ]; then
    echo "Expected $nsensors sensors in $statsf, got $ns"
    badexit
fi

# should see these numbers of raw samples
nsamps=(53 52 257 6 5 5)
rawok=true
rawsampsok=true
for (( i = 0; i < $nsensors; i++)); do
    sname=test$i

    awk "
/^$HOSTNAME:\/.*\/$sname/{
    nmatch++
}
END{
    if (nmatch != 1) {
        print \"can't find sensor $TEST/$sname in raw data_stats output\"
        exit(1)
    }
}
" $statsf || rawok=false

    nsamp=${nsamps[$i]}
    awk -v nsamp=$nsamp "
/^$HOSTNAME:\/.*\/$sname/{
    nmatch++
    if (\$4 < nsamp) {
        print \"sensor $sname, nsamps=\" \$4 \", should be \" nsamp
        # if (\$4 < nsamp - 2) exit(1)
        exit(1)
    }
}
" $statsf || rawsampsok=false
done

cat $statsf
if ! $rawok; then
    echo "${0##*/}: raw sample test failed"
else
    echo "${0##*/}: raw sample test OK"
fi

# run data through process methods
statsf=$TEST/data_stats.out
valgrind --suppressions=suppressions.txt --gen-suppressions=all --leak-check=full data_stats -l 6 -p $ofiles > $statsf

ns=`egrep "^test1" $statsf | wc | awk '{print $1}'`
if [ $ns -ne $nsensors ]; then
    echo "Expected $nsensors sensors in $statsf, got $ns"
    badexit
fi

# should see these numbers of processed samples
# The data file for the first 2 sensors has one bad record, so we
# see one less processed sample.
# The CSAT3 sonic sensor_sim sends out 1 query sample, and 256 data samples.
# The process method discards first two samples so we see 254.

# we sometimes see less than the expected number of samples.
# Needs investigation.

nsamps=(52 50 254 5 4 5)
procok=true
procsampsok=true
for (( i = 0; i < $nsensors; i++)); do
    sname=test1.t$(($i + 1))
    awk "
/^$sname/{
    nmatch++
}
END{
    if (nmatch != 1) {
        print \"can't find variable $sname in processed data_stats output\"
        exit(1)
    }
}
" $statsf || procok=false

    nsamp=${nsamps[$i]}
    awk -v nsamp=$nsamp "
/^$sname/{
    nmatch++
    if (\$4 != nsamp) {
        print \"sensor $sname, nsamps=\" \$4 \", should be \" nsamp
        # if (\$4 < nsamp - 2) exit(1)
        exit(1)
    }
}
" $statsf || procsampsok=false
done

cat $statsf

# check for valgrind errors in dsm process
dsm_errs=`valgrind_errors $TEST/dsm.log`
echo "$dsm_errs errors reported by valgrind in $TEST/dsm.log"

echo "rawok=$rawok, procok=$procok, dsm_errs=$dsm_errs"

! $procok || ! $rawok && badexit

! $procsampsok || ! $rawsampsok && badexit

if [ $dsm_errs -eq 0 ]; then
    echo "${0##*/}: serial_sensor test OK"
    exit 0
else
    cat $TEST/dsm.log
    echo "${0##*/}: serial_sensor test failed"
    badexit
fi

