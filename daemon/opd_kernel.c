/**
 * @file daemon/opd_kernel.c
 * Dealing with the kernel and kernel module samples
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 */

#include "opd_kernel.h"
#include "opd_image.h"
#include "opd_printf.h"
#include "opd_stats.h"

#include "op_fileio.h"
#include "op_config_25.h"
#include "op_libiberty.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

/* kernel module */
struct opd_module {
	char * name;
	struct opd_image * image;
	vma_t start;
	vma_t end;
	/* used when separate_kernel_samples != 0 */
	struct list_head module_list;
};

extern char * vmlinux;
extern int verbose;
extern int no_vmlinux;
extern unsigned long opd_stats[];

static struct opd_image * kernel_image;

/* kernel and module support */
static vma_t kernel_start;
static vma_t kernel_end;
static struct opd_module opd_modules[OPD_MAX_MODULES];
static unsigned int nr_modules=0;

/**
 * opd_init_kernel_image - initialise the kernel image
 */
void opd_init_kernel_image(void)
{
	/* for no vmlinux */
	if (!vmlinux)
		vmlinux = "no-vmlinux";
	kernel_image = opd_get_kernel_image(vmlinux, 0);
}


/**
 * opd_parse_kernel_range - parse the kernel range values
 */
void opd_parse_kernel_range(char const * arg)
{
	sscanf(arg, "%llx,%llx", &kernel_start, &kernel_end);

	verbprintf("OPD_PARSE_KERNEL_RANGE: kernel_start = %llx, kernel_end = %llx\n",
		   kernel_start, kernel_end);

	if (kernel_start == 0x0 || kernel_end == 0x0) {
		fprintf(stderr,
			"Warning: mis-parsed kernel range: %llx-%llx\n",
			kernel_start, kernel_end);
		fprintf(stderr, "kernel profiles will be wrong.\n");
	}
}


/**
 * new_module - initialise a module description
 *
 * @param name module name
 * @param start start address
 * @param end end address
 */
static struct opd_module * new_module(char * name, vma_t start, vma_t end)
{ 
	opd_modules[nr_modules].name = name;
	opd_modules[nr_modules].image = NULL;
	opd_modules[nr_modules].start = start;
	opd_modules[nr_modules].end = end;
	list_init(&opd_modules[nr_modules].module_list);
	nr_modules++;
	if (nr_modules == OPD_MAX_MODULES) {
		fprintf(stderr, "Exceeded %u kernel modules !\n",
		        OPD_MAX_MODULES);
		exit(EXIT_FAILURE);
	}
	return &opd_modules[nr_modules-1];
}


/**
 * opd_create_module - allocate and initialise a module description
 * @param name module name
 * @param start start address
 * @param end end address
 */
static struct opd_module *
opd_create_module(char const * name, vma_t start, vma_t end)
{
	struct opd_module * module = xmalloc(sizeof(struct opd_module));

	module->name = xstrdup(name);
	module->image = NULL;
	module->start = start;
	module->end = end;
	list_init(&module->module_list);

	return module;
}


/**
 * opd_get_module - get module structure
 * @param name  name of module image
 *
 * Find the module structure for module image name.
 * If it could not be found, add the module to
 * the global module structure.
 *
 * If an existing module is found, name is free()d.
 * Otherwise it must be freed when the module structure
 * is removed (i.e. in opd_clear_module_info()).
 */
static struct opd_module * opd_get_module(char * name)
{
	int i;

	for (i=0; i < OPD_MAX_MODULES; i++) {
		if (opd_modules[i].name && !strcmp(name, opd_modules[i].name)) {
			/* free this copy */
			free(name);
			return &opd_modules[i];
		}
	}

	return new_module(name, 0, 0);
}


/**
 * opd_clear_module_info - clear kernel module information
 *
 * Clear and free all kernel module information and reset
 * values.
 */
static void opd_clear_module_info(void)
{
	int i;

	for (i=0; i < OPD_MAX_MODULES; i++) {
		if (opd_modules[i].name)
			free(opd_modules[i].name);
		opd_modules[i].name = NULL;
		opd_modules[i].start = 0;
		opd_modules[i].end = 0;
		list_init(&opd_modules[i].module_list);
	}

	nr_modules = 0;

	opd_for_each_image(opd_delete_modules);
}


/**
 * opd_reread_module_info - parse /proc/modules for kernel modules
 *
 * Parse the file /proc/module to read start address and size of module
 * each line is in the format:
 *
 * module_name 16480 1 dependencies Live 0xe091e000
 *
 * without any blank space in each field
 *
 */
void opd_reread_module_info(void)
{
	FILE * fp;
	char * line;
	struct opd_module * mod;
	int module_size;
	char ref_count[32];
	int ret;
	char module_name[256+1];
	char live_info[32];
	char dependencies[4096];
	unsigned long long start_address;

	if (no_vmlinux)
		return;

	opd_clear_module_info();

	printf("Reading module info.\n");

	fp = op_try_open_file("/proc/modules", "r");

	if (!fp) {
		printf("oprofiled: /proc/modules not readable, "
			"can't process module samples.\n");
		return;
	}

	while (1) {
		line = op_get_line(fp);

		if (!line)
			break;

		if (line[0] == '\0') {
			free(line);
			continue;
		}

		ret = sscanf(line, "%256s %u %32s %4096s %32s %llx",
			     module_name, &module_size, ref_count,
			     dependencies, live_info, &start_address);
		if (ret != 6) {
			printf("bad /proc/modules entry: %s\n", line);
			free(line);
			continue;
		}

		mod = opd_get_module(xstrdup(module_name));
		mod->image = opd_get_kernel_image(module_name, 0);

		mod->start = start_address;
		mod->end = mod->start + module_size;

		verbprintf("module %s start %llx end %llx\n",
			   mod->name, mod->start, mod->end);

		free(line);
	}

	op_close_file(fp);
}


void opd_delete_modules(struct opd_image * image)
{
	struct list_head * pos;
	struct list_head * pos2;
	struct opd_module * module;

	verbprintf("Removing image module list for image %p\n", image);
	list_for_each_safe(pos, pos2, &image->module_list) {
		module = list_entry(pos, struct opd_module, module_list);
		if (module->name)
			free(module->name);
		free(module);
	}

	list_init(&image->module_list);
}


/**
 * opd_find_module_by_eip - find a module by its eip
 * @param eip  EIP value
 *
 * find in the modules container the module which
 * contain this eip return %NULL if not found.
 * caller must check than the module image is valid
 */
static struct opd_module * opd_find_module_by_eip(vma_t eip)
{
	uint i;
	for (i = 0; i < nr_modules; i++) {
		if (opd_modules[i].start && opd_modules[i].end &&
		    opd_modules[i].start <= eip && opd_modules[i].end > eip)
			return &opd_modules[i];
	}

	return NULL;
}


static struct opd_module *
opd_find_module(vma_t eip, struct opd_image const * app_image)
{
	struct list_head * pos;
	struct opd_module * module;
 
	list_for_each(pos, &app_image->module_list) {
		module = list_entry(pos, struct opd_module, module_list);

		if (module->start && module->end &&
		    module->start <= eip && module->end > eip) {
			return module;
		}
	}

	return 0;
}


static struct opd_image *
opd_create_app_kernel_image(struct opd_image * app_image,
                            char const * name, vma_t start, vma_t end)
{
	struct opd_image * image;
	struct opd_module * new_module;

	image = opd_get_kernel_image(name, app_image->app_name);
	if (!image) {
		verbprintf("Can't create image for %s %s\n",
			   name, app_image->app_name);
		return 0;
	}

	new_module = opd_create_module(name, start, end);
	new_module->image = image;
	list_add(&new_module->module_list, &app_image->module_list);
	return image;
}


/**
 * opd_get_app_kernel_image - find the image for a kernel sample for an app
 *
 * This adjusts the given PC value to be an offset against
 * the returned image.
 */
static struct opd_image *
opd_find_app_kernel_image(vma_t * eip, struct opd_image * app_image)
{
	struct opd_module * module = opd_find_module(*eip, app_image);
	struct opd_image * image;

	if (module) {
		opd_stats[OPD_MODULE]++;
		*eip -= module->start;
		return module->image;
	}

	if (*eip >= kernel_start && *eip < kernel_end) {
		image = opd_create_app_kernel_image(app_image, vmlinux,
		                                    kernel_start, kernel_end);
		opd_stats[OPD_KERNEL]++;
		*eip -= kernel_start;
		return image;
	}

	module = opd_find_module_by_eip(*eip);

	if (!module) {
		opd_stats[OPD_LOST_MODULE]++;
		return 0;
	}

	assert(module->image);

	if (!module->image->name) {
		opd_stats[OPD_LOST_MODULE]++;
		verbprintf("unable to get path name for module %s\n",
			   module->name);
		return 0;
	}

	opd_stats[OPD_MODULE]++;
	*eip -= module->start;
	return opd_create_app_kernel_image(app_image, module->image->name,
	                                   module->start, module->end);
}


/**
 * opd_find_kernel_image - find the image for a kernel sample
 *
 * This adjusts the given PC value to be an offset against
 * the returned image.
 */
struct opd_image *
opd_find_kernel_image(vma_t * eip, struct opd_image * app_image)
{
	if (no_vmlinux) {
		opd_stats[OPD_KERNEL]++;
		return kernel_image;
	}

	if (app_image) {
		return opd_find_app_kernel_image(eip, app_image);
	}

	struct opd_module * module;

	if (*eip >= kernel_start && *eip < kernel_end) {
		opd_stats[OPD_KERNEL]++;
		*eip -= kernel_start;
		return kernel_image;
	}

	module = opd_find_module_by_eip(*eip);

	if (!module) {
		opd_stats[OPD_LOST_MODULE]++;
		return 0;
	}

	if (module->image) {
		opd_stats[OPD_MODULE]++;
		*eip -= module->start;
		return module->image;
	}

	opd_stats[OPD_LOST_MODULE]++;
	verbprintf("No image for sampled module %s\n", module->name);
}
