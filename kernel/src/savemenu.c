#include "savemenu.h"

/* Keep this entry point deliberately small until the save menu is implemented. */
void savemenu_entry(kthread_id_t task_id, uint16_t task_segment, uint8_t console) {
	static const char message[] = "Matrix16: save menu handoff (task ";
	uint8_t i;

	(void)task_segment;
	(void)console;

	for(i = 0; message[i] != '\0'; i++)
		__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)message[i]), "Nd" ((uint16_t)0x00E9));

	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)('0' + task_id)), "Nd" ((uint16_t)0x00E9));
	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)')'), "Nd" ((uint16_t)0x00E9));
	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)'\r'), "Nd" ((uint16_t)0x00E9));
	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)'\n'), "Nd" ((uint16_t)0x00E9));
}
