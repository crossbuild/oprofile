/**
 * @file opd_parse_proc.c
 * Parsing of /proc/<pid>
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 * 
 * @author John Levon <moz@compsoc.man.ac.uk>
 * @author Philippe Elie <phil_el@wanadoo.fr>
 */

#include "opd_parse_proc.h"
#include "opd_proc.h"
#include "opd_mapping.h" 
#include "opd_image.h"
#include "opd_printf.h"
 
#include "op_file.h"
#include "op_fileio.h"
 
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
/**
 * opd_add_ascii_map - parse an ASCII map string for a process
 * @param proc  process to add map to
 * @param line  0-terminated ASCII string
 *
 * Attempt to parse the string @line for map information
 * and add the info to the process @proc. Returns %1
 * on success, %0 otherwise.
 *
 * The parsing is based on Linux 2.4 format, which looks like this :
 *
 * 4001e000-400fc000 r-xp 00000000 03:04 31011      /lib/libc-2.1.2.so
 */
/* FIXME: handle (deleted) */
static int opd_add_ascii_map(struct opd_proc * proc, char const * line)
{
	struct opd_map * map = &proc->maps[proc->nr_maps];
	char const * cp = line;

	/* skip to protection field */
	while (*cp && *cp != ' ')
		cp++;

	/* handle rwx */
	if (!*cp || (!*(++cp)) || (!*(++cp)) || (*(++cp) != 'x'))
		return 0;

	/* get start and end from "40000000-4001f000" */
	if (sscanf(line,"%x-%x", &map->start, &map->end) != 2)
		return 0;

	/* "p " */
	cp += 2;

	/* read offset */
	if (sscanf(cp,"%x", &map->offset) != 1)
		return 0;

	while (*cp && *cp != '/')
		cp++;

	if (!*cp)
		return 0;

	/* FIXME: we should verify this is indeed the primary
	 * app image by readlinking /proc/pid/exe */
	map->image = opd_get_image(cp, -1, opd_app_name(proc), 0);

	if (!map->image)
		return 0;

	if (++proc->nr_maps == proc->max_nr_maps)
		opd_grow_maps(proc);

	return 1;
}

 
/**
 * opd_get_ascii_maps - read all maps for a process
 * @param proc  process to work on
 *
 * Read the /proc/<pid>/maps file and add all
 * mapping information found to the process @proc.
 */
static void opd_get_ascii_maps(struct opd_proc * proc)
{
	FILE * fp;
	char mapsfile[20] = "/proc/";
	char * line;

	snprintf(mapsfile + 6, 6, "%hu", proc->pid);
	strcat(mapsfile,"/maps");

	fp = op_try_open_file(mapsfile, "r");
	if (!fp)
		return;

	while (1) {
		line = op_get_line(fp);
		if (!strcmp(line, "") && feof(fp)) {
			free(line);
			break;
		} else {
			opd_add_ascii_map(proc, line);
			free(line);
		}
	}

	op_close_file(fp);
}

/**
 * opd_get_ascii_procs - read process and mapping information from /proc
 *
 * Read information on each process and its mappings from the /proc
 * filesystem.
 */
void opd_get_ascii_procs(void)
{
	DIR * dir;
	struct dirent * dirent;
	struct opd_proc * proc;
	u16 pid;

	if (!(dir = opendir("/proc"))) {
		perror("oprofiled: /proc directory could not be opened. ");
		exit(EXIT_FAILURE);
	}

	while ((dirent = readdir(dir))) {
		if (sscanf(dirent->d_name, "%hu", &pid) == 1) {
			verbprintf("ASCII added %u\n", pid);
			proc = opd_add_proc(pid);
			opd_get_ascii_maps(proc);
		}
	}

	closedir(dir);
}