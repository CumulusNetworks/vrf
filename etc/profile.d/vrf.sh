VRF=$(vrf identify prompt)
export VRF

PS1='\u@\h${VRF}:\w\$ '
