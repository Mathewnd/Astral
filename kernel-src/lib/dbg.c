#include <dbg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __x86_64__
#define LOAD_BASE (uint64_t)0xffffffff80000000
#endif

#define SYM_TABLE_ENTRY_SIZE 3
extern uint32_t dbg_sym_table[];
extern uint32_t dbg_sym_table_end[];

extern char dbg_fn_name_table[];
extern char dbg_fn_name_table_end[];

extern char dbg_file_name_table[];
extern char dbg_file_name_table_end[];

void dbg_get_symbol(void *rip, char **fn_name, char **file_name, size_t *offset) {
	*fn_name = "??";
	*file_name = "??";
	*offset = 0;

	if ((uint64_t)rip < LOAD_BASE)
		return;

	uint64_t rip_offset = (uint64_t)rip - LOAD_BASE;
	size_t low = 0;
	size_t high = (dbg_sym_table_end - dbg_sym_table) / SYM_TABLE_ENTRY_SIZE;
	while (low < high) {
		size_t mid = low + (high - low) / 2;
		if (dbg_sym_table[mid * SYM_TABLE_ENTRY_SIZE] <= rip_offset)
			low = mid + 1;
		else
			high = mid;
	}

	if (low == 0)
		return;

	uint32_t *symbol = dbg_sym_table + (low - 1) * SYM_TABLE_ENTRY_SIZE;
	*fn_name = dbg_fn_name_table + symbol[1];
	*file_name = dbg_file_name_table + symbol[2];
	*offset = rip_offset - symbol[0];
}
