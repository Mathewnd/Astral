OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(kernel_entry)

PHDRS
{
	text PT_LOAD FLAGS((1 << 0) | (1 <<2)) ;
	rodata PT_LOAD FLAGS((1 << 2)) ;
	data PT_LOAD FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
	. = 0xffffffff80000000;
	_text_start = .;

	.text : {
		*(.text .text.*);
	} :text

	. = ALIGN(4K);
	_text_end = .;
	_rodata_start = .;
	.rodata : {
		*(.rodata .rodata.*);
	} :rodata

	. = ALIGN(8);
	.timekeeper_sources : {
		timekeeper_sources = .;
		*(.timekeeper_sources);
		timekeeper_sources_end = .;
	} :rodata

	. = ALIGN(8);
	.usb_class_drivers : {
		usb_class_drivers = .;
		*(.usb_class_drivers);
		usb_class_drivers_end = .;
	} :rodata

	. = ALIGN(8);
	.init_routines : {
		init_routines = .;
		*(.init_routines);
		init_routines_end = .;
	} :rodata

	. = ALIGN(4K);
	_rodata_end = .;
	_data_start = .;

	data : {
		*(.data .data.*);
	} :data

	. = ALIGN(8);
	.kernel_arguments : {
		kernel_arguments = .;
		*(.kernel_arguments);
		kernel_arguments_end = .;
	} :data

	. = ALIGN(8);
	.bss : {
		*(COMMON)
		*(.bss .bss.*)
	} :data

	. = ALIGN(4);
	.dbg_sym_table : {
		dbg_sym_table = .;
		KEEP(*(.dbg_sym_table));
		dbg_sym_table_end = .;
	} :data

	.dbg_fn_name_table : {
		dbg_fn_name_table = .;
		KEEP(*(.dbg_fn_name_table));
		dbg_fn_name_table_end = .;
	} :data

	.dbg_file_name_table : {
		dbg_file_name_table = .;
		KEEP(*(.dbg_file_name_table));
		dbg_file_name_table_end = .;
	} :data

	. = ALIGN(4K);
	_data_end = .;
}
