/*
 * VRF DNS helper
 *
 * Copyright (C) 2017 Cumulus Networks, Inc.
 * Author: David Ahern <dsa@cumulusnetworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>

#include <netlink/addr.h>
#include <netlink/cache.h>
#include <netlink/socket.h>
#include <netlink/route/rule.h>
#include <netlink/route/link.h>
#include <netlink/route/link/vrf.h>

#define RESOLVCONF	"/etc/resolv.conf"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

#define LOG_CMD(args...)			\
	if (verbose) {			\
		printf("%s: ", prog);	\
		printf(args);		\
	}

static struct nl_cache *rule_cache;
static struct nl_sock *sk;

/* priority of FIB rules. Needs to be before (lower prio)
 * VRF/l3mdev rules
 */
static uint32_t prio = 99;

static const char *prog;
static int verbose;

static int str_to_uint(const char *str, uint32_t *val)
{
	unsigned int n;
	char *end;

	errno = 0;
	n = (unsigned int) strtoul(str, &end, 0);
	if (((*end == '\0') || (*end == '\n')) && (end != str) &&
	     (errno != ERANGE)) {
		*val = n;
		return 0;
	}

	return -1;
}

static int get_vrf_table(const char *vrf, uint32_t *table)
{
	struct rtnl_link *link;
	int rc = -EINVAL;

	if (rtnl_link_get_kernel(sk, 0, vrf, &link) != 0)
		return -EINVAL;

	if (link && rtnl_link_is_vrf(link) &&
	    rtnl_link_vrf_get_tableid(link, table) == 0) {
		rc = 0;
	}

	if (link)
		rtnl_link_put(link);

	return rc;
}

static struct rtnl_rule *create_rule(struct nl_addr *addr, uint32_t table)
{
	int family = nl_addr_get_family(addr);
	struct rtnl_rule *rule;

	rule = rtnl_rule_alloc();
	if (rule) {
		rtnl_rule_set_family(rule, family);
		rtnl_rule_set_prio(rule, prio);
		rtnl_rule_set_table(rule, table);
		rtnl_rule_set_action(rule, RTN_UNICAST);
		rtnl_rule_set_dst(rule, addr);
	}

	return rule;
}

static void count_rule_in_cache(struct nl_object *obj, void *arg)
{
	int *rule_count = (int *) arg;

	*rule_count += 1;
}

static int rule_exists(struct rtnl_rule *filter)
{
	int rule_count = 0;

	nl_cache_foreach_filter(rule_cache, OBJ_CAST(filter),
				count_rule_in_cache, &rule_count);

	return rule_count;
}

static int config_dns_server(const char *dns, uint32_t table)
{
	bool try_wo_port = true;
	struct rtnl_rule *rule;
	struct nl_addr *addr;
	int err;

	err = nl_addr_parse(dns, AF_UNSPEC, &addr);
	if (err < 0) {
		fprintf(stderr, "Failed to parse address %s\n", dns);
		return -1;
	}

	rule = create_rule(addr, table);
	if (!rule) {
		err = -ENOMEM;
		goto out;
	}

	err = 0;
	if (rule_exists(rule))
		goto out;

	rtnl_rule_set_ipproto(rule, 17);
	rtnl_rule_set_dport(rule, 53);
again:
	err = rtnl_rule_add(sk, rule, 0);
	if (!err) {
		LOG_CMD("ip rule add to %s lookup %u prio %u\n",
			dns, table, prio);
	} else if (try_wo_port) {
		try_wo_port = false;
		rtnl_rule_set_ipproto(rule, 0);
		rtnl_rule_set_dport(rule, 0);
		goto again;
	} else {
		fprintf(stderr,
			"Failed to add ip rule to %s lookup %u prio %u\n",
			dns, table, prio);
	}

out:
	if (rule)
		rtnl_rule_put(rule);

	nl_addr_put(addr);

	return err;
}

static int remove_dns_server(const char *dns, uint32_t table)
{
	struct rtnl_rule *rule;
	struct nl_addr *addr;
	int err;

	err = nl_addr_parse(dns, AF_UNSPEC, &addr);
	if (err < 0) {
		fprintf(stderr, "Failed to parse DNS address\n");
		return -1;
	}

	rule = create_rule(addr, table);
	if (!rule) {
		err = -ENOMEM;
		goto out;
	}

	err = 0;
	if (!rule_exists(rule))
		goto out;

	err = rtnl_rule_delete(sk, rule, 0);
	if (!err) {
		LOG_CMD("ip rule delete to %s lookup %u prio %u\n",
			dns, table, prio);
	} else {
		fprintf(stderr,
			"Failed to delete ip rule to %s lookup %u prio %u\n",
			dns, table, prio);
	}

out:
	if (rule)
		rtnl_rule_put(rule);

	nl_addr_put(addr);

	return err;
}

static int dns_add(int argc, char *argv[])
{
	const char *dns;
	uint32_t table;

	if (argc != 3) {
		fprintf(stderr, "Invalid args for dns_add\n");
		return -EINVAL;
	}

	dns = argv[1];

	/* is the vrf arg a name or a table? */
	if (isdigit(*argv[2])) {
		if (str_to_uint(argv[2], &table) != 0) {
			fprintf(stderr, "Invalid vrf/table id\n");
			return -1;
		}
	} else if (get_vrf_table(argv[2], &table) < 0) {
		/* vrf device does not exist yet - nothing to do */
		return 0;
	}

	return config_dns_server(dns, table);
}

static int dns_del(int argc, char *argv[])
{
	uint32_t table;

	if (argc != 3) {
		fprintf(stderr, "Invalid args for dns_del\n");
		return -1;
	}

	/* is the vrf arg a name or a table? */
	if (isdigit(*argv[2])) {
		if (str_to_uint(argv[2], &table) != 0) {
			fprintf(stderr, "Invalid vrf/table id\n");
			return -1;
		}
	} else if (get_vrf_table(argv[2], &table) < 0) {
		/* vrf device does not exist - nothing to do */
		return 0;
	}

	return remove_dns_server(argv[1], table);
}

/* process each nameserver line in /etc/resolv.conf
 * if vrf is a match
 */
static int process_dns_config(const char *vrf, uint32_t table)
{
	char buf[256];
	FILE *fp;
	int err = 0;

	fp = fopen(RESOLVCONF, "r");
	if (!fp) {
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "Failed to open %s\n", RESOLVCONF);
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		char addr[32], ns_vrf[32];

		if (sscanf(buf, "nameserver %31s # vrf %31s", addr, ns_vrf) != 2)
			continue;

		if (strcmp(vrf, ns_vrf))
			continue;

		if (config_dns_server(addr, table))
			err = -1;
	}

	fclose(fp);

	return err;
}

static int vrf_dns_config_create(int argc, char *argv[])
{
	const char *vrf;
	uint32_t table;
	int err;

	if (argc < 2) {
		fprintf(stderr, "Invalid args for create\n");
		return -EINVAL;
	}

	vrf = argv[1];

	err = get_vrf_table(vrf, &table);
	if (err != 0)
		return err;

	if (argc > 2) {
		const char *tablestr = argv[2];
		uint32_t table2;

		table2 = (uint32_t) atoi(tablestr);
		if (!table2) {
			fprintf(stderr, "Invalid table id\n");
			return -EINVAL;
		}

		if (table != table2) {
			fprintf(stderr, "Table id mismatch with VRF device\n");
			return -EINVAL;
		}
	}

	return process_dns_config(vrf, table);
}

static void delete_rule_in_cache(struct nl_object *obj, void *arg)
{
	struct rtnl_rule *rule = (struct rtnl_rule *)obj;
	uint32_t table = *(uint32_t *)arg;
	struct nl_addr *addr;
	char buf[64], *pbuf;

	if (rtnl_rule_get_table(rule) != table)
		return;

	addr = rtnl_rule_get_dst(rule);
	if (addr)
		pbuf = nl_addr2str(addr, buf, sizeof(buf));
	else
		pbuf = "<unknown>";

	if (rtnl_rule_delete(sk, rule, 0) == 0) {
		LOG_CMD("ip rule delete to %s lookup %u prio %u\n",
			pbuf, table, prio);
	} else {
		fprintf(stderr,
			"Failed to delete ip rule to %s lookup %u prio %u\n",
			pbuf, table, prio);
	}
}

static int vrf_dns_config_delete(int argc, char *argv[])
{
	const char *vrf, *tablestr;
	uint32_t table;

	if (argc != 3) {
		fprintf(stderr, "Invalid args for delete\n");
		return 1;
	}

	vrf = argv[1];

	tablestr = argv[2];
	table = (uint32_t) atoi(tablestr);
	if (!table) {
		fprintf(stderr, "Invalid table id for vrf %s\n", vrf);
		return 1;
	}

	nl_cache_foreach(rule_cache, delete_rule_in_cache, &table);

	return 0;
}

static int verify_dns_config(const char *vrf, uint32_t table)
{
	char buf[256];
	FILE *fp;
	int err = 0;

	fp = fopen(RESOLVCONF, "r");
	if (!fp) {
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "Failed to open %s\n",
			RESOLVCONF);
		return 1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		char dns[32], ns_vrf[32];
		struct rtnl_rule *rule;
		struct nl_addr *addr;

		if (sscanf(buf, "nameserver %31s # vrf %31s", dns, ns_vrf) != 2)
			continue;

		if (strcmp(vrf, ns_vrf))
			continue;

		if (nl_addr_parse(dns, AF_UNSPEC, &addr) < 0)
			continue;

		rule = create_rule(addr, table);
		if (!rule) {
			fprintf(stderr,
				"Failed to allocate rule for server %s\n", dns);
			err = 1;
			continue;
		}

		if (!rule_exists(rule)) {
			fprintf(stderr, "No FIB rule for dns server %s\n", dns);
			err = 1;
		}

		rtnl_rule_put(rule);
	}

	fclose(fp);

	return err;
}

static int vrf_dns_config_verify(int argc, char *argv[])
{
	const char *vrf;
	uint32_t table;

	if (argc < 2) {
		fprintf(stderr, "Invalid args for verify\n");
		return -1;
	}

	vrf = argv[1];

	if (get_vrf_table(vrf, &table) != 0) {
		fprintf(stderr, "Invalid VRF\n");
		return 1;
	}
	if (argc > 2) {
		uint32_t table2;

		if (str_to_uint(argv[2], &table2) != 0) {
			fprintf(stderr, "Invalid table id\n");
			return 1;
		}

		if (table != table2) {
			fprintf(stderr, " Table id mismatch\n");
			return 1;
		}
	}

	return verify_dns_config(vrf, table);
}

int main(int argc, char *argv[])
{
	struct {
		char *name;
		int (*fn)(int argc, char *argv[]);
	} actions [] = {
		{ .name = "dns_add", .fn = dns_add },
		{ .name = "dns_del", .fn = dns_del },
		{ .name = "create",  .fn = vrf_dns_config_create },
		{ .name = "delete",  .fn = vrf_dns_config_delete },
		{ .name = "verify",  .fn = vrf_dns_config_verify },
	};
	const char *action;
	int rc = 1;
	int i;

	prog = basename(argv[0]);

	if (argc > 1 && !strcmp(argv[1], "-v")) {
		verbose = 1;
		argv++;
		argc--;
	}
	if (argc < 2)
		return 1;

	action = argv[1];
	argc--; argv++;

	sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "Failed to get netlink socket\n");
		return 1;
	}
	if (nl_connect(sk, NETLINK_ROUTE) < 0) {
		fprintf(stderr, "Failed to connect netlink socket\n");
		goto out;
	}

	if (rtnl_rule_alloc_cache(sk, AF_UNSPEC, &rule_cache) < 0) {
		fprintf(stderr, "Failed to populate rule cache\n");
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(actions); ++i) {
		if (!strcmp(action, actions[i].name)) {
			rc = actions[i].fn(argc, argv);
			break;
		}
	}

out:
	if (rule_cache)
		nl_cache_free(rule_cache);

	nl_socket_free(sk);

	return rc;
}
