/* COPYRIGHT (C) 2000 THE VICTORIA UNIVERSITY OF MANCHESTER and John Levon
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/ioport.h>
#include <linux/mc146818rtc.h>
 
#include "oprofile.h"

#define RTC_IO_PORTS 2
#define OP_RTC_MIN     2
#define OP_RTC_MAX  4096 

/* ---------------- RTC handler ------------------ */
 
// FIXME: share 
inline static int op_check_pid(void)
{
	if (unlikely(sysctl.pid_filter) && 
	    likely(current->pid != sysctl.pid_filter))
		return 1;

	if (unlikely(sysctl.pgrp_filter) && 
	    likely(current->pgrp != sysctl.pgrp_filter))
		return 1;
		
	return 0;
}

static void do_rtc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	uint cpu = op_cpu_id();
	unsigned char intr_flags;

	spin_lock(&rtc_lock);
	/* read and ack the interrupt */
	intr_flags = CMOS_READ(RTC_INTR_FLAGS);
	/* Is this my type of interrupt? */
	if (intr_flags & RTC_PF) {
		if (likely(!op_check_pid()))
			op_do_profile(cpu, regs, 0);
	}
	spin_unlock(&rtc_lock);
	return;
}

static int rtc_setup(void)
{
	unsigned char tmp_control;
	unsigned long flags;
	unsigned char tmp_freq_select;
	unsigned long target;
	unsigned int exp, freq;

 	spin_lock_irqsave(&rtc_lock, flags);

	/* disable periodic interrupts */
	tmp_control = CMOS_READ(RTC_CONTROL);
	tmp_control &= ~RTC_PIE;
	CMOS_WRITE(tmp_control, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);

	/* Set the frequency for periodic interrupts by finding the
	 * closest power of two within the allowed range.
	 */
 
	target = sysctl.ctr[0].count;

	exp = 0;
	while (target > (1 << exp) + ((1 << exp) >> 1))
		exp++;
	freq = 16 - exp;
 
	tmp_freq_select = CMOS_READ(RTC_FREQ_SELECT);
	tmp_freq_select = (tmp_freq_select & 0xf0) | freq;
	CMOS_WRITE(tmp_freq_select, RTC_FREQ_SELECT);

	spin_unlock_irqrestore(&rtc_lock, flags);
	return 0; 
}

static void rtc_start(void)
{
	unsigned char tmp_control;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
 
	/* Enable periodic interrupts */
	tmp_control = CMOS_READ(RTC_CONTROL);
	tmp_control |= RTC_PIE;
	CMOS_WRITE(tmp_control, RTC_CONTROL);
 
	/* read the flags register to start interrupts */
	CMOS_READ(RTC_INTR_FLAGS);

	spin_unlock_irqrestore(&rtc_lock, flags);
}

static void rtc_stop(void)
{
	unsigned char tmp_control;
	unsigned long flags;

 	spin_lock_irqsave(&rtc_lock, flags);

	/* disable periodic interrupts */
	tmp_control = CMOS_READ(RTC_CONTROL);
	tmp_control &= ~RTC_PIE;
	CMOS_WRITE(tmp_control, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);

	spin_unlock_irqrestore(&rtc_lock, flags);
}

static void rtc_start_cpu(uint cpu)
{
	rtc_start();
}

static void rtc_stop_cpu(uint cpu)
{
	rtc_stop();
}
 
static int rtc_check_params(void)
{
	int target = sysctl.ctr[0].count;

	if (target < OP_RTC_MIN || target > OP_RTC_MAX) {
		printk(KERN_ERR "RTC value %d is out of range (%d-%d)\n",
			target, OP_RTC_MIN, OP_RTC_MAX);
		return -EINVAL;
	}

	return 0;
}

static int rtc_init(void)
{
	 /* request_region returns 0 on **failure** */
	if (!request_region(RTC_PORT(0), RTC_IO_PORTS, "oprofile")) {
		printk(KERN_ERR "oprofile: can't get RTC I/O Ports\n");
		return -EBUSY;
	}

	/* request_irq returns 0 on **success** */
	if (request_irq(RTC_IRQ, do_rtc_interrupt,
			SA_INTERRUPT, "oprofile", NULL)) {
		printk(KERN_ERR "oprofile: IRQ%d busy \n", RTC_IRQ);
		release_region(RTC_PORT(0), RTC_IO_PORTS);
		return -EBUSY;
	}
	return 0;
}

static void rtc_deinit(void)
{
	free_irq(RTC_IRQ, NULL);
	release_region(RTC_PORT(0), RTC_IO_PORTS);
}
 
static int rtc_add_sysctls(ctl_table * next)
{
	*next = ((ctl_table){ 1, "rtc_value", &sysctl_parms.ctr[0].count, sizeof(int), 0600, NULL, lproc_dointvec, NULL, });
	return 0;
}

static void rtc_remove_sysctls(ctl_table * next)
{
	/* nothing to do */
}
 
struct op_int_operations op_rtc_ops = {
	init: rtc_init,
	deinit: rtc_deinit,
	add_sysctls: rtc_add_sysctls,
	remove_sysctls: rtc_remove_sysctls,
	check_params: rtc_check_params,
	setup: rtc_setup,
	start: rtc_start,
	stop: rtc_stop,
	start_cpu: rtc_start_cpu,
	stop_cpu: rtc_stop_cpu,
};
