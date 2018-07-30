/*
 * runvrf - Run a command bound to a given VRF
 *
 *          VRF binding is done via cgroups and requires root privileges to
 *          add a process id. This command serves as a setuid wrapper to add
 *          the process id to the cgroup.procs for the requested VRF and then
 *          exec the given command with arguments.
 *
 * Copyright (C) 2018 Cumulus Networks, Inc.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* Cumulus Linux 3.x with the 4.1 kernel uses an l3mdev cgroup.
 * This command as is will not run on other kernels since upstream
 * uses a different cgroup implementation for VRF bindings.
 */
#define CGRPDIR "/sys/fs/cgroup/l3mdev"

static int runcmd(int argc, char *argv[])
{
	char **new_argv;
	char *file;
	int i, rc;

	/* arg list + 1 for NULL terminator */
	new_argv = malloc((argc + 1) * sizeof(char *));
	if (!new_argv) {
		fprintf(stderr, "Failed to allocate memory for new command\n");
		return 1;
	}

	file = strdup(argv[0]);
	if (!file) {
		fprintf(stderr, "Failed to allocate memory for new command\n");
		return 1;
	}

	new_argv[0] = basename(argv[0]);
	for (i = 1; i < argc; ++i) {
		new_argv[i] = argv[i];
	}
	new_argv[i] = NULL;

	rc = execv(file, new_argv);
	if (rc < 0) {
		fprintf(stderr, "Failed to exec command: %s\n",
			strerror(errno));
	}

	return rc;
}

/* add pid to CGRPDIR/VRF/cgroup.procs */
static int setvrf(const char *vrf)
{
	char path[PATH_MAX];
	char buf[32];
	int fd, len;
	int buflen;

	buflen = snprintf(buf, sizeof(buf), "%d", getpid());
	if (buflen >= sizeof(buf)) {
		fprintf(stderr, "Invalid pid from getpid()\n");
		return 1;
	}

	/* default vrf is a special case */
	if (!strcmp(vrf, "default")) {
		len = snprintf(path, sizeof(path), CGRPDIR "/cgroup.procs");
	} else {
		len = snprintf(path, sizeof(path),
			       CGRPDIR "/%s/cgroup.procs", vrf);
	}

	if (len >= sizeof(path)) {
		fprintf(stderr, "Invalid vrf\n");
		return 1;
	}

	fd = open(path, O_WRONLY | O_APPEND | O_NOFOLLOW);
	if (fd < 0) {
		if (errno == ENOENT)
			fprintf(stderr, "Invalid vrf\n");
		else
			fprintf(stderr, "Failed to open cgroup file for vrf %s: %s\n",
				vrf, strerror(errno));
		return 1;
	}

	len = write(fd, buf, buflen);
	if (len < 0) {
		/* l3mdev returns ENODEV if master device has not been
		 * configured or is no longer valid
		 */
		if (errno == ENODEV) {
			fprintf(stderr, "cgroup for VRF is misconfigured; can not set VRF\n");
		} else {
			fprintf(stderr, "Failed to add pid to cgroup for VRF %s: %s\n",
				vrf, strerror(errno));
		}
	}

	close(fd);

	return len < 0 ? 1 : 0;
}

int main(int argc, char *argv[])
{
	int rc;

	if (argc < 3) {
		fprintf(stderr, "usage: runvrf VRF COMMAND ARGS\n");
		return 1;
	}
	if (*argv[2] != '/') {
		fprintf(stderr, "Absolute path required for command to run\n");
		return 1;
	}

	rc = setvrf(argv[1]);
	if (rc)
		return rc;

	/* drop argv[0] which is this command, and drop argv[1] which is
	 * the VRF to bind command to
	 */
	return runcmd(argc + 2, &argv[2]);
}
