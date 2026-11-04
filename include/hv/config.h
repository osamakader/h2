#ifndef HV_CONFIG_H
#define HV_CONFIG_H

/* constants shared with assembly */
#define HV_MAX_CPUS	8
#define HV_STACK_SIZE	0x4000
#define HV_MAX_VCPUS	16
#define HV_TICK_HZ	100		/* a vCPU's time slice: 10 ms */

#endif
