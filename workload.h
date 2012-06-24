#ifndef __WORKLOAD_H__
#define __WORKLOAD_H__

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdlib.h>

#include <string>

#include "cache.h"

#define CHUNK_SLOTS 1024

class workload_gen
{
public:
	/**
	 * The enxt offset in bytes.
	 */
	virtual off_t next_offset() = 0;
	virtual bool has_next() = 0;
	virtual ~workload_gen() { }
};

class seq_workload: public workload_gen
{
	long start;
	long end;
	long cur;
	int entry_size;

public:
	seq_workload(long start, long end, int entry_size) {
		this->start = start;
		this->end = end;
		this->cur = start;
		this->entry_size = entry_size;
	}

	off_t next_offset() {
		off_t next = cur;
		cur++;
		return next * entry_size;
	}

	bool has_next() {
		return cur < end;
	}
};

class rand_permute
{
	off_t *offset;
	long num;

public:
	rand_permute(long num, int stride) {
		offset = (off_t *) valloc(num * sizeof(off_t));
		for (int i = 0; i < num; i++) {
			offset[i] = ((off_t) i) * stride;
		}

		for (int i = num - 1; i >= 1; i--) {
			int j = random() % i;
			off_t tmp = offset[j];
			offset[j] = offset[i];
			offset[i] = tmp;
		}
	}

	~rand_permute() {
		free(offset);
	}

	off_t get_offset(long idx) const {
		return offset[idx];
	}
};

class stride_workload: public workload_gen
{
	long first;	// the first entry
	long last;	// the last entry but it's not included in the range
	long curr;	// the current location
	long num;	// the number of entries we have visited
	int stride;
	int entry_size;
public:
	stride_workload(long first, long last, int entry_size) {
		this->first = first;
		this->last = last;
		curr = first;
		num = 0;
		this->entry_size = entry_size;
		stride = PAGE_SIZE / entry_size;
	}

	off_t next_offset();

	bool has_next() {
		return num < (last - first);
	}
};

class local_rand_permute_workload: public workload_gen
{
	long idx;
	long num;
	long start;		// the start offset in bytes
	rand_permute *permute;

	local_rand_permute_workload() {
		idx = 0;
		num = 0;
		start = 0;
		permute = NULL;
	}

public:
	/**
	 * `start' and `end' are entry indexes.
	 */
	local_rand_permute_workload(long start, long end, int entry_size) {
		permute = new rand_permute(end - start, entry_size);
		this->start = start * entry_size;
		idx = 0;
		num = end - start;
	}

	virtual ~local_rand_permute_workload() {
		delete permute;
	}

	off_t next_offset() {
		if (idx >= num)
			return -1;
		return permute->get_offset(idx++) + start;
	}

	bool has_next() {
		return idx < num;
	}

	local_rand_permute_workload *clone() {
		local_rand_permute_workload *gen = new local_rand_permute_workload();
		gen->permute = permute;
		gen->idx = idx;
		gen->num = num;
		gen->start = start;
		return gen;
	}
};

class global_rand_permute_workload: public workload_gen
{
	long start;
	long end;
	static const rand_permute *permute;
public:
	global_rand_permute_workload(long num, int stride, long start, long end) {
		if (permute == NULL) {
			permute = new rand_permute(num, stride);
		}
		this->start = start;
		this->end = end;
	}

	virtual ~global_rand_permute_workload() {
		if (permute) {
			delete permute;
			permute = NULL;
		}
	}

	off_t next_offset() {
		if (start >= end)
			return -1;
		return permute->get_offset(start++);
	}

	bool has_next() {
		return start < end;
	}
};

/* this class reads workload from a file. */
class file_workload: public workload_gen
{
	static off_t *offsets;

	long curr;
	long end;

public:
	file_workload(const std::string &file, int nthreads);

	virtual ~file_workload() {
		free(offsets);
	}

	off_t next_offset() {
		/*
		 * the data in the file is generated by a Java program,
		 * its byte order is different from Intel architectures.
		 */
		return swap_bytesl(offsets[curr++]);
	}

	bool has_next() {
		return curr < end;
	}

	long static swap_bytesl(long num) {
		long res;

		char *src = (char *) &num;
		char *dst = (char *) &res;
		for (unsigned int i = 0; i < sizeof(long); i++) {
			dst[sizeof(long) - 1 - i] = src[i];
		}
		return res;
	}
};

class rand_workload: public workload_gen
{
	long start;
	long range;
	long num;
	off_t *offsets;
public:
	rand_workload(long start, long end, int stride) {
		this->start = start;
		this->range = end - start;
		num = 0;
		offsets = (off_t *) valloc(sizeof(*offsets) * range);
		for (int i = 0; i < range; i++) {
			offsets[i] = (start + random() % range) * stride;
		}
	}

	virtual ~rand_workload() {
		free(offsets);
	}

	off_t next_offset() {
		return offsets[num++];
	}

	bool has_next() {
		return num < range;
	}
};

/**
 * this make sure requests are evenly distributed among disks in RAID0
 * as long as the number of threads are multiple of the number of 
 * disks in RAID0.
 */
class RAID0_rand_permute_workload: public workload_gen
{
	int nthreads;
	int thread_id;
	int entry_size;
	local_rand_permute_workload *local_gen;
	static local_rand_permute_workload *gen;
public:
	RAID0_rand_permute_workload(long npages, int entry_size,
			int nthreads, int thread_id) {
		this->nthreads = nthreads;
		this->entry_size = entry_size;
		this->thread_id = thread_id;
		if (gen == NULL) {
			gen = new local_rand_permute_workload(0,
				npages * PAGE_SIZE / entry_size / nthreads, 1);
		}
		local_gen = new local_rand_permute_workload(0,
				npages * PAGE_SIZE / entry_size / nthreads, 1);
	}

	virtual ~RAID0_rand_permute_workload() {
		delete local_gen;
	}

	off_t next_offset() {
		return (thread_id + local_gen->next_offset() * nthreads) * entry_size;
	}

	bool has_next() {
		return local_gen->has_next();
	}
};

class workload_chunk
{
public:
	virtual bool get_workload(off_t *, int num) = 0;
	virtual ~workload_chunk() { }
};

class stride_workload_chunk: public workload_chunk
{
	long first;	// the first entry
	long last;	// the last entry but it's not included in the range
	long curr;	// the current location
	int stride;
	int entry_size;
	pthread_spinlock_t _lock;
public:
	stride_workload_chunk(long first, long last, int entry_size) {
		pthread_spin_init(&_lock, PTHREAD_PROCESS_PRIVATE);
		this->first = first;
		this->last = last;
		this->entry_size = entry_size;
		printf("first: %ld, last: %ld\n", first, last);
		curr = first;
		stride = PAGE_SIZE / entry_size;
	}

	virtual ~stride_workload_chunk() {
		pthread_spin_destroy(&_lock);
	}

	bool get_workload(off_t *offsets, int num);
};

class balanced_workload: public workload_gen
{
	off_t offsets[CHUNK_SLOTS];
	int curr;
	static workload_chunk *chunks;
public:
	balanced_workload(workload_chunk *chunks) {
		memset(offsets, 0, sizeof(offsets));
		curr = CHUNK_SLOTS;
		this->chunks = chunks;
	}

	virtual ~balanced_workload() {
		if (chunks) {
			delete chunks;
			chunks = NULL;
		}
	}

	off_t next_offset() {
		return offsets[curr++];
	}

	bool has_next() {
		if (curr < CHUNK_SLOTS)
			return true;
		else {
			bool ret = chunks->get_workload(offsets, CHUNK_SLOTS);
			curr = 0;
			return ret;
		}
	}
};

#endif
