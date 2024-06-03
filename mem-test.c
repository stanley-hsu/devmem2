/*
 * devmem2.c: Simple program to read/write from/to any location in memory.
 *
 *  Copyright (C) 2000, Jan-Derk Bakker (jdb@lartmaker.nl)
 *
 *
 * This software has been developed for the LART computing board
 * (http://www.lart.tudelft.nl/). The development has been sponsored by
 * the Mobile MultiMedia Communications (http://www.mmc.tudelft.nl/)
 * and Ubiquitous Communications (http://www.ubicom.tudelft.nl/)
 * projects.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
  __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

#define CACHE_LINE_SIZE 64
#define CACHE_LINE_MASK (~(CACHE_LINE_SIZE - 1))

//#define FAKE_MAP

/* some how, 4KB memcpy is wired */
#define BUFF_SIZE_DW (CACHE_LINE_SIZE/sizeof(unsigned int))

struct support_function_t {
	const char *name;
	void (*function)(void *, unsigned int data);
};

void write_cache_line(void *ptr, void *data)
{
	memcpy(ptr, data, CACHE_LINE_SIZE);
}

void read_cache_line(void *ptr, void *data)
{
	memcpy(data, ptr, CACHE_LINE_SIZE);
}

unsigned int prepare_data_addr(int buf[BUFF_SIZE_DW], unsigned int data)
{
	int i;
	for (i = 0; i < BUFF_SIZE_DW; i++) {
		buf[i] = data++;
	}
	return data;
}

void read_range(void *base, unsigned int byte_size)
{
	unsigned int i;

	for (i = 0; i < byte_size / CACHE_LINE_SIZE; i++) {
		unsigned int readdata[BUFF_SIZE_DW];
		int j;

		read_cache_line(base + i * CACHE_LINE_SIZE, readdata);
		for (j = 0; j < BUFF_SIZE_DW; j += 4) {
			printf("%08x: 0x%08x 0x%08x 0x%08x 0x08%x\n", i * CACHE_LINE_SIZE,
				readdata[j + 0], readdata[j + 1], readdata[j + 2], readdata[j + 3]);
		}
	}
}

void fill(void *base, unsigned int data, int byte_size)
{
	unsigned int i;

	printf("start fill %x\n", byte_size);
	for (i = 0; i < byte_size / CACHE_LINE_SIZE; i++) {
		unsigned int buff[BUFF_SIZE_DW];

		data = prepare_data_addr(buff, data);
		printf("write off %x\n", i * CACHE_LINE_SIZE);
		write_cache_line(base + i * CACHE_LINE_SIZE, buff);
	}
}

void verify(void *base, unsigned int data, int byte_size)
{
	unsigned int i;
	for (i = 0; i < byte_size / CACHE_LINE_SIZE; i++) {
		unsigned int expected[BUFF_SIZE_DW];
		unsigned int readback[BUFF_SIZE_DW];
		unsigned int j;

		data = prepare_data_addr(expected, data);
		read_cache_line(base + i * CACHE_LINE_SIZE, readback);

		for (j = 0; j < BUFF_SIZE_DW; j++) {
			if (expected[j] != readback[j]) {
				fprintf(stderr, " fail at base %x, expected %x, but %x\n", i * CACHE_LINE_SIZE, expected[j], readback[j]);
				return;
			}
		}
	}
	printf("verify %d done\n", byte_size);
}

void fill_4k(void *base, unsigned int data)
{
	fill(base, data, 4096);
	verify(base, data, 4096);
}

void read_4k(void *base, unsigned int data)
{
	read_range(base, 4096);
}

struct support_function_t support_functions[] = {
	{"fill-4k", fill_4k},
	{"read-4k", read_4k}
};

void *get_target_base(off_t target, int *fd)
{
	void *map_base = NULL;
	int i;

#if defined(FAKE_MAP)
	printf("\nTEST mode: use fake memory\n");
	map_base = malloc(MAP_SIZE);
	if (!map_base) {
		fprintf(stderr, "No memory available\n");
		exit(1);
	}
	for (i = 0; i < MAP_SIZE / sizeof(unsigned short); i++) {
		*((unsigned short *)(map_base) + i) = i;
	}
#else
	int _fd;
	if((_fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
	*fd = _fd;
	printf("/dev/mem opened.\n");
	fflush(stdout);

	/* Map one page */
	map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, target & ~MAP_MASK);
	if(map_base == (void *) -1) FATAL;
	printf("Memory mapped at address %p.\n", map_base);
	fflush(stdout);
#endif
	return map_base;
}

int main(int argc, char **argv) {
	int fd;
	void *map_base;
	off_t target;
	int max_test_id = sizeof(support_functions) / sizeof(struct support_function_t);
	int test_id = 0;
	unsigned int data = 0;
	int i, c;

	if(argc < 2) {
		fprintf(stderr, "\nUsage:\t%s { address } [ type [ data ] ]\n"
			"\taddress : memory address to act upon\n"
			"\test_id  : 0~%d\n"
			"\tdata    : data to be written\n\n",
			argv[0], max_test_id - 1);
		exit(1);
	}

	target = strtoul(argv[1], 0, 0);
	if(argc > 2)
		test_id = strtoul(argv[2], 0, 0);

	if (argc > 3)
		data = strtoul(argv[3], 0, 0);

	if (test_id >= max_test_id) {
		fprintf(stderr, "no this test %d\n", test_id);
		exit(1);
	}

	map_base = get_target_base(target, &fd);
	printf("run %s...\n", support_functions[test_id].name);
	support_functions[test_id].function(map_base, data);

#if defined(FAKE_MAP)
	free(map_base);
#else
	if(munmap(map_base, MAP_SIZE) == -1) FATAL;
	close(fd);
#endif
	return 0;
}

