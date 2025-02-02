#!/bin/sh

PYTHON=${PYTHON:-python}

# Usage info
show_help() {
cat << EOF
Usage: ${0##*/} [-h] [-p ESPTOOL_PORT] [-P PYTHON] -f FILENAME
Flash image file to device, leave existing system intact."

    -h               Display this help and exit
    -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerrous).
    -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: "$PYTHON")
    -f FILENAME      The .bin file to flash.  Custom to your device type and region.
EOF
}


while getopts ":hp:P:f:" opt; do
    case "${opt}" in
        h)
            show_help
            exit 0
            ;;
        p)  export ESPTOOL_PORT=${OPTARG}
	    ;;
        P)  PYTHON=${OPTARG}
            ;;
        f)  FILENAME=${OPTARG}
            ;;
        *)
 	    echo "Invalid flag."
            show_help >&2
            exit 1
            ;;
    esac
done
shift "$((OPTIND-1))"

if [ -f "${FILENAME}" ]; then
	echo "Trying to flash update ${FILENAME}."
	$PYTHON -m esptool --baud 921600 write_flash 0x10000 ${FILENAME}
	echo "Erasing the otadata partition, which will turn off flash flippy-flop and force the first image to be used"
	$PYTHON -m esptool --baud 921600 erase_region 0xe000 0x2000
else
	echo "Invalid file: ${FILENAME}"
	show_help
fi

exit 0
