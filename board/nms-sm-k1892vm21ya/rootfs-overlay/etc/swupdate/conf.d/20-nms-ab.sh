# Make the swupdate daemon - and therefore the web interface on :8080 -
# A/B aware.
#
# sw-description keeps everything inside the "stable.A" / "stable.B"
# sections, so a package uploaded through the web UI is rejected with
# "Found nothing to install" unless a selection is given.  The web form
# has no way to ask which slot to use, so the daemon always targets the
# inactive one: the same slot nms-update would pick, and never the slot
# the system is running from.
#
# The installation itself is finished by nms-postupdate, which flips the
# extlinux.conf default once swupdate reports success.
#
# Note that the Buildroot wrapper assembles SWUPDATE_ARGS before sourcing
# the conf.d files, so the selection has to be appended to SWUPDATE_ARGS
# here; setting SWUPDATE_EXTRA_ARGS would be too late.

. /usr/sbin/nms-ab

if slot=$(nms_ab_other_slot) && nms_ab_prepare "$slot"; then
	SWUPDATE_ARGS="$SWUPDATE_ARGS -e stable,$slot -p /usr/sbin/nms-postupdate"
fi
