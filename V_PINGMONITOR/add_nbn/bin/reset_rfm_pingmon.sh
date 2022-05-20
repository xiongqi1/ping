#!/bin/sh

RDM_STATUS=`rdb_get link.profile.1.status`
ACCESS_SEEKER_STATUS=`rdb_get link.profile.2.status`

# FIXME: TT18369. WMMD does not yet support modem reset.
# This needs to be verified again once that support has
# been added. For now, do a full reboot instead.
#RESET_CMD="rdb_set wwan.0.reset 1"
RESET_CMD="rdb_set service.system.reset 1"

if [ "$RDM_STATUS" = "up" -o "$ACCESS_SEEKER_STATUS" = "up" ]; then
	$RESET_CMD
	rdb_set service.systemmonitor.pingmon_dccdwait_cnt 0
else
	DCCD_WAIT_MAX=`rdb_get service.systemmonitor.pingmon_dccdwait_max`
	if [ "$DCCD_WAIT_MAX" != "999999" ]; then
		CUR_WAIT_CNT=`rdb_get service.systemmonitor.pingmon_dccdwait_cnt`
		if [ -z $CUR_WAIT_CNT ]; then
			CUR_WAIT_CNT=0
		fi
		OVER_WAIT_MAX=`expr ${CUR_WAIT_CNT} \>= ${DCCD_WAIT_MAX}`
		if [ "$OVER_WAIT_MAX" = "1" ]; then
			$RESET_CMD	
			/usr/bin/rdb_set service.systemmonitor.pingmon_dccdwait_cnt 0
		else
			INC_WAIT_CNT=`expr ${CUR_WAIT_CNT}  + 1`
			/usr/bin/rdb_set service.systemmonitor.pingmon_dccdwait_cnt "$INC_WAIT_CNT"
		fi	
	fi
fi
exit 0
