===========
cl-mgmtvrf
===========

-------------------------
Configure Management VRF
-------------------------

:Author: Cumulus Networks <support@cumulusnetworks.com>
:Date:   2015-09-01
:Copyright: Copyright 2015 Cumulus Networks, Inc.  All rights reserved.
:Version: 0.3
:Manual section: 8

SYNOPSIS
========

    **cl-mgmtvrf** [-e | --enable]
    **cl-mgmtvrf** [-d | --disable]
    **cl-mgmtvrf** [-s | --status]
    **cl-mgmtvrf** [-h | --help]

DESCRIPTION
===========
    **cl-mgmtvrf** is used to configure a separate routing table for use with
    the management interface. 

    In many deployments, a separate out-of-band network is employed to carry 
    management traffic. For example, orchestration agents such as Ansible, 
    Chef, and Puppet, use the management network to communicate. Similarly,
    DNS, and monitoring tools such as SNMP also communicate over this 
    management network.

    This means, that there can be multiple default routes, one for the 
    management network and another for the dataplane network. To allow
    for the use of multiple default routes, separate routing tables are used,
    one for each kind of network. **cl-mgmtvrf** provides a separate routing
    table for management and dataplane networks.

    While communication is over the management interface by default, when
    management VRF is enabled, applications have complete access to all
    the device states, information and routes across the management and the
    dataplane.
    
    Management routes are stored in *mgmt* table and data plane routes are
    stored in *main* table.


OPTIONS
=======

    -e, --enable      configure the use of a separate routing table for eth0

    -d, --disable     unconfigure the use of a separate routing table for eth0

    -s, --status      display whether eth0 uses a separate routing table

    -h, --help        display usage message

EXAMPLES
========
    # Configure Management VRF, allowing eth0 to use a separate routing table

        **cl-mgmtvrf --enable**

    # Undo the use of a separate routing table for eth0

        **cl-mgmtvrf --disable**

    # Check running state of Management VRF

        **cl-mgmtvrf --status**

    # See the management routing table

        **ip route show table mgmt**

    # See the dataplane routing table
       
        **ip route show**
	**ip route show table main**

KNOWN_ISSUES
============
    **cl-mgmtvrf** is currently experimental.

    **cl-mgmtvrf** has been found to work in all of the common deployment
    models. But its possible that it doesn't do as you expect it to in 
    certain conditions. Some well-known conditions are as follows:

    * Only eth0 is supported as the management interface today. Inband 
      management interface is **not** supported.
    * Unless an application binds to a source IP address, routing lookups 
      are performed in the **mgmt** routing table. Another way to make the
      routing lookups be performed over the **main** routing table is to
      bind to an interface (such as using socket's SO_BINDTODEVICE).
    * ping and traceroute (and their IPv6 cousins) by default use the
      **main** routing table. If you want lookups performed over the 
      **mgmt** routing table, use the -I eth0(for ping) or the -i eth0
      (for traceroute).
    * ip route get looks up the *mgmt* table by default when 
      management VRF is enabled. To use ip route get to use the main
      routing table, use the *from* option and use say, the loopback IP 
      address. For example:
      **ip route get 21.1.1.1 from 10.1.1.2**
    * DNS over eth0 doesn't work with ping/traceroute if eth0 is not 
      configured via DHCP.

SEE ALSO
========
    ip rule(8)
    ip route(8)
    ping(8)
    traceroute(1)

