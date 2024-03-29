#!/bin/sh

dockerns=ncar   # namespace on docker.io

usage() {
    echo "${0##*/} [--no-cache] [-p]
    --no-cache: dont' use podman cache when building images
    -p: push image to docker.io/$dockerns. You may need to do: podman login docker.io
    "
    exit 1
}

cacheFlag=""
dopush=false
arches=(armbe)
while [ $# -gt 0 ]; do
    case $1 in
        --no-cache)
            cacheFlag="--no-cache"
            ;;
        -p)
            dopush=true
            ;;
        *)
            usage
            ;;
    esac
    shift
done

# Build a docker image of Debian Jessie for doing C/C++ debian builds for
# various targets, such as armel and armhf (RPi).
# The image is built from the Dockerfile.cross_arm in this directory.

set -e

for arch in ${arches[*]}; do

    version=1
    tag=ael_v$version

    image=nidas-build-ael-$arch
    echo "arch is $arch"
    echo "image is $image"
    echo "tagged image is $dockerns/$image:$tag"

    podman build $cacheFlag -t $dockerns/$image:$tag \
        -f Dockerfile.cross_ael_armbe .

    # Only push if the build worked
    if [[ "$?" -eq 0 ]] ; then
        if $dopush; then
            echo "Pushing $dockerns/$image:$tag docker://docker.io/$dockerns/$image:$tag"
            podman push $dockerns/$image:$tag docker://docker.io/$dockerns/$image:$tag && echo "push success"
        fi
    fi

done
