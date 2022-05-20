#!/bin/sh

#
# This script is called by periodicpingd that is launched by periodicping.template 
#

#
# periodic_reboot : 0 (ping failure) , 1 (periodic reboot)
#

periodic_reboot="$1"

log() {
	logger -t "reboot-after-random-delay.sh" -- "$@"
}

run() {
	log "$*"
	"$@" 2>&1 | log
}

script=$(basename "$0")

print_usage() {
	cat << EOF

This script is called by periodicpingd that is launched by periodicping.template 

usage>
	$script <1|0>

options>
	0:
		ping failure

	1:
		periodic reboot

	-h, --help:
		Print a help text

EOF
}

log "start"

case "$periodic_reboot" in
	'1')
		log "* ping failure reboot"
		
		log "rebooting.."
		run "/usr/bin/rdb_set" "service.system.reset" "1"
		;;

	'0')
		log "* periodic reboot"
		
		# get random configuration
		rndmin=$(rdb_get "service.systemmonitor.forcereset.rndmin")
		if [ -z "$rndmin" ]; then
			rndmin="0"
		fi

		# get sec
		rndsec=$(random.sh "$(($rndmin*60))" )

		# schedule timer
		log "schedule reboot timer $rndsec second(s) later"
		runontime "periodic_reboot_timer" "$rndsec" "/usr/bin/rdb_set" "service.system.reset" "1" 2> /dev/null > /dev/null & 
		;;

	'-h'|'--help')
		print_usage >&2
		;;

	*)
		log "no failure type specified"
		print_usage >&2
		;;
esac

log "done"

exit 0
