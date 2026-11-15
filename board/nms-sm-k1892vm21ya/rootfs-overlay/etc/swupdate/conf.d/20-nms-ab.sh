# Same inactive slot for web uploads and swupdate-client.
slot=$(/usr/sbin/nms-update target) || exit 1
SWUPDATE_ARGS="$SWUPDATE_ARGS -e stable,$slot"
