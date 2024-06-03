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

#define FAKE_MAP

/* some how, 4KB memcpy is wired */
#define BUFF_SIZE_DW (CACHE_LINE_SIZE/sizeof(unsigned long))

int main(int argc, char **argv) {
	int fd;
	void *map_base, *virt_addr;
	unsigned long writeval;
	unsigned long read_result[BUFF_SIZE_DW], writevalues[BUFF_SIZE_DW];
	off_t target;
	int access_type = 'w';
	int i, c;

	if(argc < 2) {
		fprintf(stderr, "\nUsage:\t%s { address } [ type [ data ] ]\n"
			"\taddress : memory address to act upon\n"
			"\ttype    : access operation type : [b]yte, [h]alfword, [w]ord\n"
			"\tdata    : data to be written\n\n",
			argv[0]);
		exit(1);
	}

	target = strtoul(argv[1], 0, 0);
	if(argc > 2)
		access_type = tolower(argv[2][0]);

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
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
	printf("/dev/mem opened.\n");
	fflush(stdout);

	/* Map one page */
	map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK);
	if(map_base == (void *) -1) FATAL;
	printf("Memory mapped at address %p.\n", map_base);
	fflush(stdout);
#endif
	virt_addr = map_base + (target & MAP_MASK);
	c = 1;
	switch(access_type) {
		case 'b':
			read_result[0] = *((unsigned char *) virt_addr);
			break;
		case 'h':
			read_result[0] = *((unsigned short *) virt_addr);
			break;
		case 'w':
			read_result[0] = *((unsigned long *) virt_addr);
			break;
		case 'c':
			virt_addr = map_base + ((target & MAP_MASK) & CACHE_LINE_MASK);
			for (i = 0; i < BUFF_SIZE_DW; i++) {
				read_result[i] = *(((unsigned long *) virt_addr) + i);
			}
			c = BUFF_SIZE_DW;
			break;
		default:
			fprintf(stderr, "Illegal data type '%c'.\n", access_type);
			exit(2);
	}
	for (i = 0; i < c; i++) {
		printf("Value at address 0x%lx (%p): 0x%lx\n", target, virt_addr + i * sizeof(unsigned long), read_result[i]);
	}
	fflush(stdout);

	if(argc > 3) {
		writeval = strtoul(argv[3], 0, 0);
		switch(access_type) {
			case 'b':
				*((unsigned char *) virt_addr) = writeval;
				read_result[0] = *((unsigned char *) virt_addr);
				printf("Written 0x%lx; readback 0x%lx\n", writeval, read_result[0]);
				break;
			case 'h':
				*((unsigned short *) virt_addr) = writeval;
				read_result[0] = *((unsigned short *) virt_addr);
				printf("Written 0x%lx; readback 0x%lx\n", writeval, read_result[0]);
				break;
			case 'w':
				*((unsigned long *) virt_addr) = writeval;
				read_result[0] = *((unsigned long *) virt_addr);
				printf("Written 0x%lx; readback 0x%lx\n", writeval, read_result[0]);
				break;
			case 'c':
#define write_cache_value_setup(type, value_array, value)	\
	type *write_data = (type *) &value_array;				\
	for (i = 0; i < CACHE_LINE_SIZE / sizeof(type); i++) {	\
		write_data[i] = (type)value + i;				\
	}
				if (writeval <= 0xFFFF) {
					write_cache_value_setup(unsigned short, writevalues, writeval);
				} else if (writeval <= 0xFFFFFFFF) {
					write_cache_value_setup(unsigned int, writevalues, writeval);
				} else {
					write_cache_value_setup(unsigned long, writevalues, writeval);
				}
				memcpy(virt_addr, writevalues, CACHE_LINE_SIZE);
				for (i = 0; i < BUFF_SIZE_DW; i++) {
					read_result[i] = *(((unsigned long *) virt_addr) + i);
				}
				for (i = 0; i < BUFF_SIZE_DW; i++) {
					printf("Value at address 0x%lx (%p): Written 0x%lx readback 0x%lx",
						target, virt_addr + i * sizeof(unsigned long), writevalues[i], read_result[i]);

					if (read_result[i] == writevalues[i]) {
						printf("\n");
					} else {
						printf(", mismatch\n");
					}
				}
				break;
		}
		fflush(stdout);
	}

#if defined(FAKE_MAP)
	free(map_base);
#else
	if(munmap(map_base, MAP_SIZE) == -1) FATAL;
	close(fd);
#endif
	return 0;
}

