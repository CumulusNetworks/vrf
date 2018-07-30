#!/usr/bin/env bash

# Wrapper to run a command in default VRF if various conditions
# are not met. Assumes original commands are diverted to
# /usr/share/mgmt-vrf.

export PATH=/usr/share/mgmt-vrf/bin:/usr/share/mgmt-vrf/usr/bin:$PATH

# wrapper can only be used to run ping and traceroute
CMD="$1"
shift
case "$CMD" in
	ping|ping6|traceroute|traceroute6) :;;
	*) echo "Invalid command"; exit 1;;
esac

# get name of management vrf
mgmt=$(mgmt-vrf name)

# get vrf we are running in
vrf=$(vrf id)

# Want all commands to default to front panel ports using default
# VRF and user specifies argument for other VRF context. So if we
# are running in mgmt VRF switch to default VRF and run command.

if [ "$vrf" = "$mgmt" ]; then
	sudo vrf task set default $$
fi

exec $CMD $*
