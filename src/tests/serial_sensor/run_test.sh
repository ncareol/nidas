#!/bin/bash

# Test script for a dsm process, sampling serial sensors, via pseudo-terminals

# -------------- GLOBALS --------------

# scons may not set HOSTNAME
export HOSTNAME=`hostname`
vgopts="--suppressions=suppressions.txt --gen-suppressions=all --leak-check=full"
valgrind="valgrind $vgopts"
sspids=()
dsmpid=
# The version of xmlrpc we're using on bionic debian does not do a pselect/ppoll
# when it waits for connections, meaning that it can't atomically detect
# a signal and exit.  So on these systems don't start xmlrpc thread with
# the -r option, because dsm_server won't shut down with a simple TERM signal,
# and has to be killed with -9, which causes the test to fail.
source /etc/os-release
xmlrpcopt=
[ "$ID" != ubuntu ] && xmlrpcopt=-r
dsm_errs=0
svr_errs=0

debugging=false
alltests="test_serial_dsm_server test_serial_dsm"
testnames=
while [ $# -gt 0 ]; do
    case "$1" in
        -d) debugging=true ;;
        *) testnames="$testnames $1" ;;
    esac
    shift
done
echo testnames=$testnames

source ../nidas_tests.sh

check_executable dsm
check_executable dsm_server

if $debugging; then
    export TEST=/tmp/test_debug
    mkdir -p $TEST
else
    export TEST=$(mktemp -d --tmpdir test_XXXXXX)
    trap "{ rm -rf $TEST; }" EXIT
fi
echo "TEST=$TEST"

logs="--log info"
$debugging && logs="--log debug"

start_dsm() # config
{
    config="$1"

    rm -f $TEST/dsm.log
    rm -f $TEST/${HOSTNAME}_*

    # start dsm data collection
    rm -f $TEST/dsm.pid
    (set -x
     $valgrind dsm -d --pid $TEST/dsm.pid $logs $config 2>&1 | \
     tee $TEST/dsm.log ) &
    dsmpid=$!
}

kill_dsm()
{
    echo "doing kill_dsm"
    # send a TERM signal to dsm process
    nkill=0
    dsmpid=`pgrep -f "valgrind.* dsm -d"`
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

start_dsm_server()
{
    rm -f $TEST/dsm_server.log
    rm -f $TEST/server_*

    export NIDAS_CONFIGS=config/configs.xml
    # valgrind --tool=helgrind dsm_server -d -l 6 -r -c > $TEST/dsm_server.log 2>&1 &
    # --gen-suppressions=all
    (set -x; exec $valgrind dsm_server -d $logs $xmlrpcopt -c > $TEST/dsm_server.log 2>&1) &

    # seems like this should be synchronized on something, but I'm not sure
    # what.
    sleep 10
}


kill_dsm_server() {
    # send a TERM signal to valgrind dsm_server process
    nkill=0
    dsmpid=`pgrep -f "valgrind.* dsm_server"`
    if [ -n "$dsmpid" ]; then
        echo "Doing kill -TERM $dsmpid"
        kill -TERM $dsmpid
        while ps -p $dsmpid -f; do
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


remove_devices() {
    for dev in $*; do
        rm -f $TEST/$dev
    done
}


wait_for_devices() {
    nsleeps=0
    for dev in $* ; do
        while [ ! -e "$TEST/$dev" ]; do
            if [ $nsleeps -ge 10 ]; then
                echo "Missing devices after 10 seconds!"
                exit 1
            fi
            echo "waiting for device $TEST/${dev}..."
            nsleeps=$(($nsleeps + 1))
            sleep 1
        done
    done
    echo "Devices found: $*"
}


find_udp_port() {
    which netstat >& /dev/null || { echo "netstat not found, install net-tools" && exit 1; }
    local -a inuse=(`netstat -uan | awk '/^udp/{print $4}' | sed -r 's/.*:([0-9]+)$/\1/' | sort -u`)
    local port1=$(( $(cat /proc/sys/net/ipv4/ip_local_port_range | awk '{print $1}') - 1))
    for (( port = $port1; ; port--)); do
        echo ${inuse[*]} | grep -F -q $port || break
    done
    echo $port
}

export NIDAS_SVC_PORT_UDP=`find_udp_port`
echo "Using port=$NIDAS_SVC_PORT_UDP"


badexit() {
    if $debugging; then
        echo "See test debug directory: $TEST"
    else
        save=/tmp/test_save1
        echo "Saving $TEST as $save"
        [ -d $save ] && rm -rf $save
        mv $TEST $save
    fi
    exit 1
}


start_sim() # device [arg ...]
{
    device="$1"
    shift
    # default to waiting for HUP to clear on pts.  It can be overridden if -C
    # is passed to skip the wait, but really there's no reason for the
    # simulator not to wait for the dsm to open the pts.
    (set -x;
     exec sensor_sim -v -a 60 -H -t "$@" "$TEST/$device" > $TEST/${device}.log 2>&1) &
    pid=$!
    sspids=(${sspids[*]} $pid)
    nsensors=${#sspids[*]}
    echo started sensor $TEST/$device, pid $pid, nsensors=$nsensors
}


wait_on_sims()
{
    # technically this could be wait ${pids[*]}, but a loop might be useful to
    # diagnose failures
    for (( n = 0; n < $nsensors; n++ )); do
        echo "waiting on ${sspids[$n]} for $TEST/test$n"
        wait ${sspids[$n]}
    done
    # although the sims have written all their data, we don't know if the dsm
    # (and possibly dsm_server in turn) have read all the data, and there's
    # not really an easy way to check.  we know the final dat files should be
    # 27860 bytes (bz2 files 3050), however the archive files are not
    # flushed until the dsm and dsm_server processes are shut down, so that
    # doesn't help. so resort to sleeping, hopefully this is long enough.
    sleep 5
}


start_sensors()
{
    sspids=()
    devices="test0 test1 test2 test3 test4 test5"
    remove_devices $devices

    # even though the sonic on test2 and prompted sensor on test5 will wait before
    # sending data, they cannot start reading until the other pty is open, so they
    # do not use -C either.
    start_sim test0 -f data/test.dat -e "\n" -r 10
    start_sim test1 -f data/test.dat -b $'\e' -r 10
    # simulate Campbell sonic
    start_sim test2 -c -r 60 -n 256
    start_sim test3 -f data/repeated_sep.dat -e xxy -r 1
    start_sim test4 -f data/repeated_sep.dat -b xxy -r 1
    start_sim test5 -f data/repeated_sep.dat -p "hello\n" -e "\n"
    # make sure the pseudo terminals have been created before starting dsm
    wait_for_devices $devices
}


check_output() # fileprefix
{
    fp=$1
    # check output data file for the expected number of samples
    ofiles=($TEST/${fp}_*)
    if [ ${#ofiles[*]} -ne 1 ]; then
        echo "Expected one output file, got ${#ofiles[*]}"
        badexit
    fi

    # run data_stats on raw data file
    statsf=$TEST/data_stats_${fp}.out
    data_stats $ofiles > $statsf

    ns=`grep -E "^$HOSTNAME:$TEST/test" $statsf | wc | awk '{print $1}'`
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
                if (\$4 < nsamp) {
                    print \"sensor $sname, nsamps=\" \$4 \", should be \" nsamp
                    exit(1)
                }
            }
    " $statsf || rawsampsok=false
    done

    cat $statsf
    if ! $rawok || ! $rawsampsok; then
        echo "${0##*/}: raw sample test failed"
    else
        echo "${0##*/}: raw sample test OK"
    fi

    # run data through process methods
    data_stats -p $ofiles > $statsf

    ns=`grep -E "^test1" $statsf | wc | awk '{print $1}'`
    if [ $ns -ne $nsensors ]; then
        echo "Expected $nsensors sensors in $statsf, got $ns"
        badexit
    fi

    # should see these numbers of processed samples
    # The data file for the first 2 sensors has one bad record, so we
    # see one less processed sample.
    # The CSAT3 sonic sensor_sim sends out 1 query sample, and 256 data samples.
    # The process method discards first two samples so we see 254.

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
                if (\$4 != nsamp) {
                    print \"sensor $sname, nsamps=\" \$4 \", should be \" nsamp
                    exit(1)
                }
            }
            " $statsf || procsampsok=false
    done

    cat $statsf

    if ! $procok || ! $procsampsok; then
        echo "${0##*/}: proc sample test failed"
    else
        echo "${0##*/}: proc sample test OK"
    fi

    if [ $fp != server ]; then
        # check for valgrind errors in dsm process
        dsm_errs=`valgrind_errors $TEST/dsm.log`
        echo "$dsm_errs errors reported by valgrind in $TEST/dsm.log"
    else
        # check for valgrind errors in dsm_server
        if [ -f $TEST/dsm_server.log ]; then
            svr_errs=`valgrind_errors $TEST/dsm_server.log`
            echo "$svr_errs errors reported by valgrind in $TEST/dsm_server.log"
        fi
    fi
}


finish_test()
{
    ! $procok || ! $rawok && badexit

    ! $procsampsok || ! $rawsampsok && badexit

    if [ $dsm_errs -eq 0 -a $svr_errs -eq 0 ]; then
        echo "${0##*/}: $test_name OK"
    else
        [ $dsm_errs -gt 0 ] && cat $TEST/dsm.log
        [ $svr_errs -gt 0 ] && cat $TEST/dsm_server.log
        echo "${0##*/}: $test_name failed"
        badexit
    fi
}


# Previously, this script scanned for "opening $TEST/testN" messages in the
# dsm.log to know when it was safe to signal the simulators to continue.
# However, the "opening" message is actually logged before the open() attempt,
# so there was a race condition between when the simulator started writing to
# the pty and when the dsm opened it.  This is now synchronized by using a new
# -H option to sensor_sim, so sensor_sim does not continue until the HUP is
# cleared, meaning the dsm has opened it's port.

# This synchronization ensures that the DSM does not write the sonic data mode
# prompts until sensor_sim is ready to read them, and sensor_sim does not
# start writing data messages until dsm is ready to read them.

# Since sensor_sim exits when it is done writing data, that is enough to
# synchronize when the dsm should be stopped.  Previously this script scanned
# for "closed" messages for each of the ports, but that could cause confusion
# when a sensor was closed because sensor_sim was not responding soon enough
# (ie, a sonic was not answering to data mode prompts and not reporting data).
# (It would be hard to guarantee that sensor_sim can respond like a hardware
# sensor, since it is subject to the process scheduler just like dsm.)

# It is safe to wait on the sensor_sim processes to finish, because they are
# guaranteed to exit on the alarm timeout.

# There is also no way to guarantee that all the sensors will even be opened
# at the same time.  It is possible (and has been observed) that some of the
# sensor_sim processes finish before the other ports have been opened.  Since
# port opens are handled by a different dsm thread and not synchronized in any
# way, the ordering depends entirely on the scheduler.  To synchronize the
# simulated sensors with each other, so their data are interleaved in the dsm
# reads, the sensor_sim processes would have to be synchronized somehow, such
# as by all running in one process.

test_serial_dsm()
{
    start_sensors
    start_dsm config/test.xml
    wait_on_sims
    kill_dsm
    check_output
    finish_test
}


test_serial_dsm_server()
{
    # like test_serial_dsm, but run dsm_server too
    start_sensors
    start_dsm_server
    start_dsm sock:localhost:$NIDAS_SVC_PORT_UDP
    wait_on_sims
    kill_dsm
    # dsm flushes it's last samples on shutdown, so give dsm_server time to
    # read those last samples before shutting it down, which in turn finally
    # flushes those samples to the archive file.  this is all speculative and
    # not deterministic of course, since we have no way of synchronizing with
    # the actual reads and writes of the samples, but the final tests depend
    # on every sample getting through.
    sleep 10
    kill_dsm_server
    check_output $HOSTNAME
    check_output server
    finish_test
}


if [ -z "$testnames" ]; then
    testnames=test_serial_dsm_server
    echo "Running default test: $testnames"
fi

for test in $testnames ; do
    test_name=$test
    case "$test" in
        test_serial_dsm|dsm)
            test_serial_dsm
            ;;
        test_serial_dsm_server|dsm_server)
            test_serial_dsm_server
            ;;
    esac
done
