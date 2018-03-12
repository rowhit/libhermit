/*
 * Copyright (c) 2010-2017, Stefan Lankes, RWTH Aachen University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the University nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <hermit/stdio.h>
#include <hermit/string.h>
#include <hermit/processor.h>
#include <hermit/time.h>
#include <hermit/tasks.h>
#include <hermit/errno.h>
#include <hermit/spinlock.h>
#include <hermit/logging.h>
#include <asm/irq.h>

/*
 * This will keep track of how many ticks the system
 * has been running for
 */
DEFINE_PER_CORE(uint64_t, timer_ticks, 0);
static uint32_t freq_hz;  /* frequency in Hz (updates per second) */

extern uint32_t cpu_freq;
#if 0
extern int32_t boot_processor;
#endif

#define MHZ 1000000

static void restart_periodic_timer(void)
{
	set_cntp_tval(freq_hz / TIMER_FREQ);
	set_cntp_ctl(1);
}

#ifdef DYNAMIC_TICKS
DEFINE_PER_CORE(uint64_t, last_tick, 0);
static uint64_t boot_tick = 0;

void check_ticks(void)
{
	// do we already know the timer frequency? => if not, ignore this check
	if (!freq_hz)
		return;

	const uint64_t curr_tick = get_cntpct();
	rmb();

	uint64_t diff_ticks = curr_tick - per_core(last_tick);
	diff_ticks = (diff_ticks * (uint64_t) TIMER_FREQ) / freq_hz;

	if (diff_ticks > 0) {
		set_per_core(timer_ticks, per_core(timer_ticks) + diff_ticks);
		set_per_core(last_tick, curr_tick);
		rmb();
	}
}
#endif

/*
 * Handles the timer. In this case, it's very simple: We
 * increment the 'timer_ticks' variable every time the
 * timer fires.
 */
static void timer_handler(struct state *s)
{
#ifndef DYNAMIC_TICKS
	/* Increment our 'tick counter' */
	set_per_core(timer_ticks, per_core(timer_ticks)+1);
	restart_periodic_timer();
#else
	/* stop timer */
	set_cntp_ctl(0);
#endif

#if 1
	/*
	 * Every TIMER_FREQ clocks (approximately 1 second), we will
	 * display a message on the screen
	 */
	if (timer_ticks % TIMER_FREQ == 0) {
		LOG_INFO("One second has passed %d\n", CORE_ID);
	}
#endif
}

int timer_wait(unsigned int ticks)
{
	uint64_t eticks = per_core(timer_ticks) + ticks;

	task_t* curr_task = per_core(current_task);

	if (curr_task->status == TASK_IDLE)
	{
		/*
		 * This will continuously loop until the given time has
		 * been reached
		 */
		while (per_core(timer_ticks) < eticks) {
			check_workqueues();

			// recheck break condition
			if (per_core(timer_ticks) >= eticks)
				break;

			PAUSE;
		}
	} else if (per_core(timer_ticks) < eticks) {
		check_workqueues();

		if (per_core(timer_ticks) < eticks) {
			set_timer(eticks);
			reschedule();
		}
	}

	return 0;
}

/*
 * Sets up the system clock
 */
int timer_init(void)
{
	LOG_INFO("Set system counter frequency to %d MHz\n", 1);

	freq_hz = get_cntfrq();

	LOG_INFO("aarch64_timer: frequency %d KHz\n", freq_hz / 1000);

	irq_install_handler(INT_PPI_NSPHYS_TIMER, timer_handler);

#ifdef DYNAMIC_TICKS
    boot_tick = get_cntpct();
    set_per_core(last_tick, boot_tick);
#else
	restart_periodic_timer();
#endif

	return 0;
}