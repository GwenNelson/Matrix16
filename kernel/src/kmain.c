#include <stdint.h>
#include <stdbool.h>
#include "bioscons.h"
#include "biosdisk.h"
#include "isr.h"
#include "kpanic.h"
#include "kthread.h"
#include "string.h"
#include "utils.h"

typedef struct rootfs_dir_t {
	uint16_t start_lba;
	uint16_t end_lba;
	uint16_t size;
	char name[26];
} __attribute__((packed)) rootfs_dir_t;

char buf[512];


void k_timer_callback(void) {
	kthread_schedule();
}
uint16_t fs_read_rootfs_block(uint16_t lba, uint16_t es, uint16_t bx) {
	uint16_t c;
	uint16_t h;
	uint16_t s;
	lba2chs(lba,&c,&h,&s);
	return bios_read_sector(es,bx,c,h,s,1);
}

static rootfs_dir_t* dir_buf;

static rootfs_dir_t known_files[32];

bool fs_mount_rootdir(uint16_t start, uint16_t sec_count) {
	if(fs_read_rootfs_block(start,0x1000,(uint16_t)&buf) != 0) {
		bios_puts("Failed to read rootdir!\n");
		return false;
	}
	dir_buf = (rootfs_dir_t*)buf;
	
	int i=0;
	for(i=0; i<16; i++) { /* for now we only bother with the first sector */
		if(dir_buf[i].name[0] != '\0') {
			bios_puts("\t Found file: ");
			bios_puts(dir_buf[i].name);  // I know this is shitty code, i should use a memcpy or something, stfu
			bios_puts("\n");
			known_files[i].start_lba = dir_buf[i].start_lba;
			known_files[i].end_lba   = dir_buf[i].end_lba;
			known_files[i].size      = dir_buf[i].size;
			for(int j = 0; j < sizeof(known_files[i].name); j++)
				known_files[i].name[j] = dir_buf[i].name[j];
		}
	}
	return true;
}

static uint16_t rootfs_dir_start;
static uint16_t rootfs_dir_sectors;

bool fs_check_super(void) {
	bios_puts("Checking rootfs in B:...\n");
	if(fs_read_rootfs_block(0,0x1000,(uint16_t)&buf) != 0) {
		bios_puts("Failed to read superblock!\n");
		return false;
	}

	if( (buf[0] != 'M') || (buf[1] != 'A') || (buf[2] != 'T') || (buf[3] != '1') || (buf[4] != '6') ) {
		bios_puts("Bad signature on superblock!\n");
		return false;
	}

	if( (buf[5] != '\0') || (buf[6] != '\0') || (buf[7] != '\0') ) {
		bios_puts("Bad padding bytes on superblock!\n");
		return false;
	}

	uint16_t* dir_start   = (uint16_t*)&(buf[8]);
	uint16_t* dir_sectors = (uint16_t*)&(buf[10]);

	bios_puts("Found a valid Matrix16 filesystem superblock in B:\n");

	bios_puts("\tdir_start: ");
	bios_put_decimal(*dir_start);
	bios_puts(", dir_sectors: ");
	bios_put_decimal(*dir_sectors);
	bios_puts("\n");

	rootfs_dir_start   = *dir_start;
	rootfs_dir_sectors = *dir_sectors;
	return true;
}

bool fs_locate_file(char* name, uint16_t es, char* buf, uint16_t *len) {
	if(name == 0 || len == 0)
		return false;

	for(uint16_t i = 0; i < 16; i++) {
		if(known_files[i].name[0] == '\0')
			continue;

		if(strcmp(name, known_files[i].name) == 0) {
			uint16_t dest = (uint16_t)(uintptr_t)buf;

			for(uint16_t lba = known_files[i].start_lba;
			    lba < known_files[i].end_lba; lba++) {
				if(fs_read_rootfs_block(lba, es, dest) != 0)
					return false;
				dest += 512;
			}

			*len = known_files[i].size;
			return true;
		}
	}

	return false;
}

char system_cfg_buf[1024]; // yes, this will crash horribly if SYSTEM.CFG is too big, i know
uint16_t system_cfg_len;

char system_cfg_shell[26];
uint16_t system_cfg_consoles = 4;

void load_system_cfg(void) {
	if(!fs_locate_file("SYSTEM.CFG",0x1000,(char*)system_cfg_buf,&system_cfg_len)) {
		bios_puts("Could not open SYSTEM.CFG, using defaults....\n");
		strcpy(system_cfg_shell, "SHELL.PRG");
		return;
	}

	system_cfg_buf[system_cfg_len] = '\0';
	char *line = system_cfg_buf;
	while(*line != '\0') {
		char *next = strchr(line, '\n');
		if(next != 0) {
			*next = '\0';
			next++;
		} else {
			next = strchr(line, '\0');
		}

		char *cr = strchr(line, '\r');
		if(cr != 0)
			*cr = '\0';

		if(strncmp(line, "SHELL=", 6) == 0) {
			strcpy(system_cfg_shell, line + 6);
		} else if(strncmp(line, "CONSOLES=", 9) == 0) {
			char *value = line + 9;
			char *digit = value;
			uint16_t consoles = 0;
			uint8_t valid = 1;

			while(*digit != '\0') {
				if(*digit < '0' || *digit > '9') {
					valid = 0;
					break;
				}
				consoles = consoles * 10 + (*digit - '0');
				digit++;
			}
			if(valid && digit != value)
				system_cfg_consoles = consoles;
		}

		line = next;
	}

	if(system_cfg_consoles > 4)
		kpanic("maximum of 4 consoles allowed!");
}



// the shell is configured to run at 2000:0000 - that is, the very first task after the kernel itself
void load_default_shell() {
	uint16_t shell_len;
	if(!fs_locate_file(system_cfg_shell,0x2000,(char*)0x0000, &shell_len)) {
		kpanic("MISSING SHELL!");
	}
	// if we get here, yay! it should be possible to just JMP to it
	__asm__ volatile (
	    "ljmp $0x2000, $0x0000"
	);	
}

void kernel_main(void) {
	bios_puts("Matrix16 Kernel loaded!\n\n");

	install_isr08();
	install_isr09();
	install_isr80();

	// gonna test matfs16 here, then come back to the other stuff



/*	bios_puts("Starting up kthread...\n");
	kthread_init();
	bios_puts("Should be ready to start schedule...\n");
	kthread_start();
	kpanic("Something went wrong!");*/

	/*bool got_rootfs = false;

	if(fs_check_super()) {
		bios_puts("Attempting to mount rootfs...\n");
		got_rootfs = fs_mount_rootdir(rootfs_dir_start,rootfs_dir_sectors);
	}

	if(got_rootfs) {
		load_system_cfg();
	}

	bios_puts("Starting configured shell B:");
	bios_puts(system_cfg_shell);
	bios_puts("...\n");

	load_default_shell();*/

	for(;;);
}
