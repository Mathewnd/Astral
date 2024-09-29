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

	. = ALIGN(4K);
	_rodata_end = .;
	_data_start = .;

	data : {
		*(.data .data.*);
	} :data

	. = ALIGN(8);
	.bss : {
		*(COMMON)
		*(.bss .bss.*)
	} :data
	
	. = ALIGN(4K);
	_data_end = .;
}
