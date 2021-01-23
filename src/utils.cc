
/**
 * @file utils.cc
 * @author Hajime Suzuki
 */
#include "utils.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#ifndef GIT_COMMIT
#  define GIT_COMMIT		"unknown"
#endif

/*
 * trap for illegal instruction exception, see utils.h for how it's used in the bench
 */
#include <setjmp.h>
jmp_buf jb;

static
void sigill_trap(int s) {
	(void)s;
	siglongjmp(jb, 1);		/* just return back to the caller */
}

static
void init_sigill_trap(void) {
	struct sigaction a;
	memset(&a, 0, sizeof(struct sigaction));
	a.sa_handler = sigill_trap;
	sigemptyset(&a.sa_mask);
	sigaddset(&a.sa_mask, SIGILL);
	sigaction(SIGILL, &a, NULL);
	return;
}

/*
 * affinity API
 */
#if defined(__APPLE__)
#include <pthread.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>

static
void init_process_affinity(size_t core) {

	printf("binding to core %zu\n", core);
	pthread_t th = pthread_self();
	thread_affinity_policy_data_t const policy = { (int)core };
	thread_port_t mach_thread = pthread_mach_thread_np(th);
	thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY, (thread_policy_t)&policy, 1);
	return;
}

#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>

static
void init_process_affinity(size_t core) {
	(void)core;
	return;
}

#else
static
void init_process_affinity(size_t core) {
	/* just ignore it when it's not available */
	(void)core;
	return;
}

#endif



void init(bool md, size_t core) {
	init_sigill_trap();
	init_process_affinity(core);

	notes n(md, "AArch64 latency / throughput benchmark report", 0);
	n.put("Generated by https://github.com/ocxtal/insn_bench_aarch64 (commit: %s).", GIT_COMMIT);
	return;
}

/*
 * run command and dump output
 */
static
char *run_command(char const *cmd) {
	FILE *fp = popen(cmd, "r");
	if(fp == NULL) {
		return(NULL);
	}

	size_t const buf_size = 1024 * 1024;
	uint8_t *buf = (uint8_t *)malloc(buf_size);

	size_t cap = 1024, used = 0;
	uint8_t *dst = (uint8_t *)malloc(cap);

    while(fgets((char *)buf, buf_size - 1, fp) != NULL) {
        size_t const bytes = strlen((char *)buf);
		if(used + bytes >= cap) {
			dst = (uint8_t *)realloc((void *)dst, (cap *= 2));
		}
		memcpy(&dst[used], buf, bytes);
		dst[used += bytes] = 0;
    }
	free(buf);
	pclose(fp);
	return((char *)dst);
}

void dump_uname_a(bool md) {
	notes n(md, "uname -a");

	char const *cmd = "uname -a";
	n.put("`%s`:", cmd);

	char *s = run_command(cmd);
	if(s) { n.quote(s); } else { n.put("(not available)"); }
	free(s);
	return;
}

void dump_cpuinfo(bool md) {
	notes n(md, "Processor inforomation");

	#if defined(__APPLE__)
	char const *cmd = "system_profiler SPHardwareDataType";
	#elif defined(__linux__)
	char const *cmd = "lscpu";
	#endif

	n.put("`%s`:", cmd);

	char *s = run_command(cmd);
	if(s) { n.quote(s); } else { n.put("(not available)"); }
	free(s);
	return;
}


/*
 * I assume the processor has 1 cycle latency for 64bit add.
 */
#define ADD_LATENCY_CYCLES		( (size_t)1 )

/*
 * estimate CPU frequency from a sequence of adds. it assumes the processor
 * does not scale the frequency. otherwise the result becomes unreliable.
 */
static inline
double estimate_cpu_freq_core(void) {
	double const coef = 100000000.0;
	bench b(coef, (size_t)0, 0, 0, 0, 25, 1, 1);

	double r = 0.0;
	for(size_t i = 0; i < 3; i++) {
		r = b.lat_(0, op( g->add(d->x, s->x, 1) )).lat;
	}
	return(coef / ((double)ADD_LATENCY_CYCLES * r));
}

double estimate_cpu_freq(bool md, size_t trials) {
	notes n(md, "CPU frequency estimation");
	n.put("measuring CPU frequency, assuming latency of 64bit addition is %zu cycle(s):", ADD_LATENCY_CYCLES);
	n.newline();

	double sum = 0.0;
	for(size_t i = 0; i < trials; i++) {
		double const f = estimate_cpu_freq_core();
		n.item("%.2f MHz", f / 1000000.0);
		sum += f;
	}
	return(sum / (double)trials);
}

/*
 * end of utils.cc
 */
