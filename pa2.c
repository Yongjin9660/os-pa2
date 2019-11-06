/**********************************************************************
 * Copyright (c) 2019
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

/* THIS FILE IS ALL YOURS; DO WHATEVER YOU WANT TO DO IN THIS FILE */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "types.h"
#include "list_head.h"

/**
 * The process which is currently running
 */
#include "process.h"
extern struct process *current;

/**
 * List head to hold the processes ready to run
 */
extern struct list_head readyqueue;


/**
 * Resources in the system.
 */
#include "resource.h"
extern struct resource resources[NR_RESOURCES];


/**
 * Monotonically increasing ticks
 */
extern unsigned int ticks;


/**
 * Quiet mode. True if the program was started with -q option
 */
extern bool quiet;


/***********************************************************************
 * Default FCFS resource acquision function
 *
 * DESCRIPTION
 *   This is the default resource acquision function which is called back
 *   whenever the current process is to acquire resource @resource_id.
 *   The current implementation serves the resource in the requesting order
 *   without considering the priority. See the comments in sched.h
 ***********************************************************************/
bool fcfs_acquire(int resource_id)
{
	struct resource *r = resources + resource_id;

	if (!r->owner) {
		/* This resource is not owned by any one. Take it! */
		r->owner = current;
		return true;
	}

	/* OK, this resource is taken by @r->owner. */

	/* Update the current process state */
	current->status = PROCESS_WAIT;

	/* And append current to waitqueue */
	list_add_tail(&current->list, &r->waitqueue);

	/**
	 * And return false to indicate the resource is not available.
	 * The scheduler framework will soon call schedule() function to
	 * schedule out current and to pick the next process to run.
	 */
	return false;
}

/***********************************************************************
 * Default FCFS resource release function
 *
 * DESCRIPTION
 *   This is the default resource release function which is called back
 *   whenever the current process is to release resource @resource_id.
 *   The current implementation serves the resource in the requesting order
 *   without considering the priority. See the comments in sched.h
 ***********************************************************************/
void fcfs_release(int resource_id)
{
	struct resource *r = resources + resource_id;

	/* Ensure that the owner process is releasing the resource */
	assert(r->owner == current);

	/* Un-own this resource */
	r->owner = NULL;

	/* Let's wake up ONE waiter (if exists) that came first */
	if (!list_empty(&r->waitqueue)) {
		struct process *waiter =
				list_first_entry(&r->waitqueue, struct process, list);

		/**
		 * Ensure the waiter  is in the wait status
		 */
		assert(waiter->status == PROCESS_WAIT);

		/**
		 * Take out the waiter from the waiting queue. Note we use
		 * list_del_init() over list_del() to maintain the list head tidy
		 * (otherwise, the framework will complain on the list head
		 * when the process exits).
		 */
		list_del_init(&waiter->list);

		/* Update the process status */
		waiter->status = PROCESS_READY;

		/**
		 * Put the waiter process into ready queue. The framework will
		 * do the rest.
		 */
		list_add_tail(&waiter->list, &readyqueue);
	}
}



#include "sched.h"

/***********************************************************************
 * FIFO scheduler
 ***********************************************************************/
static int fifo_initialize(void)
{
	return 0;
}

static void fifo_finalize(void)
{
}

static struct process *fifo_schedule(void)
{
	struct process *next = NULL;

	/* You may inspect the situation by calling dump_status() at any time */
	// dump_status();

	/**
	 * When there was no process to run in the previous tick (so does
	 * in the very beginning of the simulation), there will be
	 * no @current process. In this case, pick the next without examining
	 * the current process. Also, when the current process is blocked
	 * while acquiring a resource, @current is (supposed to be) attached
	 * to the waitqueue of the corresponding resource. In this case just
	 * pick the next as well.
	 */
	if (!current || current->status == PROCESS_WAIT) {
		goto pick_next;
	}

	/* The current process has remaining lifetime. Schedule it again */
	if (current->age < current->lifespan) {
		return current;
	}

pick_next:
	/* Let's pick a new process to run next */

	if (!list_empty(&readyqueue)) {
		/**
		 * If the ready queue is not empty, pick the first process
		 * in the ready queue
		 */
		next = list_first_entry(&readyqueue, struct process, list);

		/**
		 * Detach the process from the ready queue. Note we use list_del_init()
		 * instead of list_del() to maintain the list head tidy. Otherwise,
		 * the framework will complain (assert) on process exit.
		 */
		list_del_init(&next->list);
	}

	/* Return the next process to run */
	return next;
}

struct scheduler fifo_scheduler = {
	.name = "FIFO",
	.acquire = fcfs_acquire,
	.release = fcfs_release,
	.initialize = fifo_initialize,
	.finalize = fifo_finalize,
	.schedule = fifo_schedule,
};


/***********************************************************************
 * SJF scheduler
 ***********************************************************************/
static struct process *sjf_schedule(void)
{
	struct process *next = NULL;
	unsigned int min = 100000;

	if (!current || current->status == PROCESS_WAIT) {
		goto pick_next;
	}
	if (current->age < current->lifespan) {
		return current;
	}
pick_next:
	if (!list_empty(&readyqueue)) {
		list_for_each_entry(next, &readyqueue, list) {
			if (min > next->lifespan) min = next->lifespan;
		}
		list_for_each_entry(next, &readyqueue, list) {
			if (min == next->lifespan) break;
		}
		list_del_init(&next->list);
	}
	return next;
}

struct scheduler sjf_scheduler = {
	.name = "Shortest-Job First",
	.acquire = fcfs_acquire, /* Use the default FCFS acquire() */
	.release = fcfs_release, /* Use the default FCFS release() */
	.schedule = sjf_schedule,		 /* TODO: Assign sjf_schedule()
								to this function pointer to activate
								SJF in the system */
};

/***********************************************************************
 * SRTF scheduler
 ***********************************************************************/
static struct process *srtf_schedule(void)
{
	struct process *next = NULL;
	unsigned int min = 100000;

	if (!list_empty(&readyqueue)) {
		list_for_each_entry(next, &readyqueue, list) {
			if (min > (next->lifespan - next->age))
				min = next->lifespan - next->age;
		}
		list_for_each_entry(next, &readyqueue, list) {
			if (min == (next->lifespan - next->age))
				break;
		}
	}
	if (!current || current->status == PROCESS_WAIT) {
		goto pick_next;
	}
	if (current->age < current->lifespan) {
		if (current->lifespan - current->age > min) {
			list_add_tail(&current->list, &readyqueue);
			goto pick_next;
		}
		return current;
	}
pick_next:
	if (!list_empty(&readyqueue)) {
		list_del_init(&next->list);
	}
	return next;
}
struct scheduler srtf_scheduler = {
	.name = "Shortest Remaining Time First",
	.acquire = fcfs_acquire, /* Use the default FCFS acquire() */
	.release = fcfs_release, /* Use the default FCFS release() */
	.schedule = srtf_schedule,
	/* You need to check the newly created processes to implement SRTF.
	 * Use @forked() callback to mark newly created processes */
	/* Obviously, you should implement srtf_schedule() and attach it here */
};


/***********************************************************************
 * Round-robin scheduler
 ***********************************************************************/
static struct process *rr_schedule(void) {
	struct process *next = NULL;

	if (!current || current->status == PROCESS_WAIT || current->age == current->lifespan) {
		goto pick_next;
	}
	if (current->age < current->lifespan && list_empty(&readyqueue)) {
		return current;
	}
	if (!list_empty(&readyqueue)) {
		list_add_tail(&current->list, &readyqueue);
		goto pick_next;
	}
pick_next:
	if (!list_empty(&readyqueue)) {
		next = list_first_entry(&readyqueue, struct process, list);
		list_del_init(&next->list);
	}
	return next;
}
struct scheduler rr_scheduler = {
	.name = "Round-Robin",
	.acquire = fcfs_acquire, /* Use the default FCFS acquire() */
	.release = fcfs_release, /* Use the default FCFS release() */
	.schedule = rr_schedule,/* Obviously, you should implement rr_schedule() and attach it here */
};

/***********************************************************************
 * Priority scheduler
 ***********************************************************************/
static struct process *prio_schedule(void){
	struct process *next = NULL;
	struct process *temp;
	unsigned int max;
	unsigned int check=0;
	if (!current || current->status == PROCESS_WAIT || current->age == current->lifespan) {
		goto pick_next;
	}
	if (current->age < current->lifespan && list_empty(&readyqueue)) {
		return current;
	}
	if (!list_empty(&readyqueue)) {
		max = current->prio_orig;
		list_for_each_entry(next, &readyqueue, list){
			if(max < next->prio_orig) max = next->prio_orig;
		}
		list_for_each_entry(next, &readyqueue, list){
			if(max == next->prio_orig){
				check=1;
				break;
			}
		}
		if(check==1){
			list_add_tail(&current->list, &readyqueue);
			list_del_init(&next->list);

		}else{
			return current;
		}
	}
	return next;

pick_next:
	if (!list_empty(&readyqueue)) {
		temp = list_first_entry(&readyqueue, struct process, list);
		max = temp->prio_orig;

		list_for_each_entry(next, &readyqueue, list){
			if(max < next->prio_orig) max = next->prio_orig;
		}
		list_for_each_entry(next, &readyqueue, list){
			if(max == next->prio_orig) break;
		}
		list_del_init(&next->list);
	}
	return next;
}

bool prio_acquire(int resource_id)
{
	struct resource *r = resources + resource_id;
	if (!r->owner) {
		r->owner = current;
		return true;
	}
	current->status = PROCESS_WAIT;
	list_add_tail(&current->list, &r->waitqueue);
	return false;
}

void prio_release(int resource_id)
{
	struct resource *r = resources + resource_id;
	struct process *p;
	
	unsigned int max;
	assert(r->owner == current);

	r->owner = NULL;

	if (!list_empty(&r->waitqueue)) {
		p = list_first_entry(&r->waitqueue, struct process, list);
		max = p->prio_orig;

		list_for_each_entry(p, &r->waitqueue, list){
			if(p->prio_orig > max) max = p->prio_orig;
		}
		list_for_each_entry(p, &r->waitqueue, list){
			if(p->prio_orig == max)
				break;
		}
		struct process *p = list_first_entry(&r->waitqueue, struct process, list);

		assert(p->status == PROCESS_WAIT);

		list_del_init(&p->list);

		p->status = PROCESS_READY;

		list_add_tail(&p->list, &readyqueue);
	}
}

struct scheduler prio_scheduler = {
	.name = "Priority",
	.acquire = prio_acquire,
	.release = prio_release,
	.schedule = prio_schedule,
};

/***********************************************************************
 * Priority scheduler with priority inheritance protocol
 ***********************************************************************/
static struct process *pip_schedule(void){
	struct process *next = NULL;
	struct process *temp;
	unsigned int max;
	unsigned int check = 0;

	if (!current || current->status == PROCESS_WAIT || current->age == current->lifespan) {
		goto pick_next;
	}
	if (current->age < current->lifespan && list_empty(&readyqueue)) {
		return current;
	}
	if (!list_empty(&readyqueue)) {
		max = current->prio;
		list_for_each_entry(next, &readyqueue, list){
			if(max < next->prio) max = next->prio;
		}
		list_for_each_entry(next, &readyqueue, list){
			if(max == next->prio){
				check=1;
				break;
			}
		}
		if(check==1){
			list_add_tail(&current->list, &readyqueue);
			list_del_init(&next->list);

		}else{
			return current;
		}
	}
	return next;

pick_next:
	if (!list_empty(&readyqueue)) {
		temp = list_first_entry(&readyqueue, struct process, list);
		max = temp->prio;

		list_for_each_entry(next, &readyqueue, list){
			if(max < next->prio) max = next->prio;
		}
		list_for_each_entry(next, &readyqueue, list){
			if(max == next->prio) break;
		}
		list_del_init(&next->list);
	}
	return next;
}

bool pip_acquire(int resource_id)
{
	struct resource *r = resources + resource_id;
	if (!r->owner) {
		r->owner = current;
		return true;
	}
	current->status = PROCESS_WAIT;

	if(r->owner->prio < current->prio){
		r->owner->prio = current->prio;
	}

	list_add_tail(&current->list, &r->waitqueue);
	return false;
}

void pip_release(int resource_id)
{
	struct resource *r = resources + resource_id;
	struct process *p;
	
	unsigned int max;
	assert(r->owner == current);
	r->owner->prio = r->owner->prio_orig;
	r->owner = NULL;
	
	if (!list_empty(&r->waitqueue)) {
		p = list_first_entry(&r->waitqueue, struct process, list);
		max = p->prio;

		list_for_each_entry(p, &r->waitqueue, list){
			if(p->prio > max) max = p->prio;
		}
		list_for_each_entry(p, &r->waitqueue, list){
			if(p->prio == max)
				break;
		}
		
		assert(p->status == PROCESS_WAIT);
		list_del_init(&p->list);

		struct resource_schedule *rs, *tmp;
		unsigned int acn=0;
		unsigned int hon=0;
		struct process *temp;
		list_for_each_entry(temp, &p->__resources_to_acquire, list){
			acn++;
		}
		list_for_each_entry(temp, &p->__resources_to_acquire, list){
			hon++;
		}
		if(acn == hon){
			p->status = PROCESS_READY;
			list_add_tail(&p->list, &readyqueue);
			
		}
	}
}

struct scheduler pip_scheduler = {
	.name = "Priority + Priority Inheritance Protocol",
	.acquire = pip_acquire,
	.release = pip_release,
	.schedule = pip_schedule,
	/* It goes without saying to implement your own pip_schedule() */
};
