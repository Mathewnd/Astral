#!/usr/bin/env python3
# USAGE:
# python3 gen_fn_sym.py kernel_exec base_addr source_cwd output_file
#
# - output_file is an ELF object exporting dbg_sym_table, dbg_fn_name_table,
#   dbg_file_name_table, and their corresponding _end symbols.
# - Each symbol record is <uint32_t address, uint32_t function_name_offset,
#   uint32_t file_name_offset>. Addresses are relative to base_addr and string
#   offsets are byte offsets into the corresponding string table.
# - Select matching tools with --nm, --objdump and --objcopy (or NM,
#   OBJDUMP and OBJCOPY envs).

import argparse
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
from typing import NamedTuple

UINT32_MAX = (1 << 32) - 1
SECTION_MARKERS = {"_text_start", "_text_end"}

class BfdTarget(NamedTuple):
	file_format: str
	architecture: str

def run_tool(command):
	return subprocess.run(
		command, check=True, capture_output=True,
		encoding="utf-8", errors="surrogateescape",
		env={**os.environ, "LC_ALL": "C"},
	).stdout

def tool_command(value):
	command = shlex.split(value)
	if not command:
		raise argparse.ArgumentTypeError("tool command must not be empty")

	return command

def get_bfd_target(kernel_exec, objdump):
	output = run_tool([*objdump, "-f", "--", kernel_exec])
	file_format = architecture = None

	for line in output.splitlines():
		if "file format " in line:
			file_format = line.rsplit("file format ", 1)[1].strip()

		if line.startswith("architecture: "):
			architecture = line[len("architecture: "):].split(",", 1)[0].strip()

	if not file_format or not architecture or architecture == "UNKNOWN!":
		raise ValueError("objdump could not identify the kernel target; select a matching --objdump")

	return BfdTarget(file_format, architecture)

def source_filename(location, source_prefix):
	if not location:
		return ""

	path, separator, line_number = location.rpartition(":")
	filename = path if separator and line_number.isdigit() else location
	filename = os.path.normpath(filename)

	if filename.startswith(source_prefix):
		filename = filename[len(source_prefix):]

	return filename

def collect_symbols(kernel_exec, base_addr, source_cwd, nm):
	output = run_tool(
		[*nm, "-n", "-l", "--defined-only", "--format=bsd",
		 "--no-demangle", "--radix=x", "--", kernel_exec],
	)
	source_prefix = os.path.abspath(source_cwd).rstrip(os.sep) + os.sep
	symbols = []

	for line in output.splitlines():
		symbol_text, _, location = line.partition("\t")
		fields = symbol_text.split(None, 2)

		if len(fields) != 3 or fields[1] not in ("T", "t"):
			continue

		address, _, name = fields
		if name in SECTION_MARKERS:
			continue

		offset = int(address, 16) - base_addr
		if not 0 <= offset <= UINT32_MAX:
			raise ValueError(f"address of {name} does not fit in uint32_t relative to base_addr")

		filename = source_filename(location, source_prefix)
		symbols.append((offset, name, filename))

	if not symbols:
		raise ValueError("kernel binary contains no text symbols")
    
	return sorted(symbols, key=lambda symbol: (symbol[0], symbol[1]))

def intern_string(value, table, offsets):
	if value not in offsets:
		encoded = value.encode("utf-8", errors="surrogateescape") + b"\0"
		if len(table) + len(encoded) - 1 > UINT32_MAX:
			raise ValueError("string table does not fit in uint32_t offsets")

		offsets[value] = len(table)
		table.extend(encoded)

	return offsets[value]

def make_tables(symbols):
	records = bytearray()
	functions = bytearray(b"\0")
	files = bytearray(b"\0")
	function_offsets = {"": 0}
	file_offsets = {"": 0}

	for address, name, filename in symbols:
		function_offset = intern_string(name, functions, function_offsets)
		file_offset = intern_string(filename, files, file_offsets)
		records.extend(struct.pack("<III", address, function_offset, file_offset))

	return (
		("dbg_sym_table", records),
		("dbg_fn_name_table", functions),
		("dbg_file_name_table", files),
	)

def write_object(tables, output_file, objcopy, bfd_target):
	output = Path(output_file).absolute()

	with tempfile.TemporaryDirectory(prefix=".gen_fn_sym-", dir=output.parent) as directory:
		directory = Path(directory)
		name, data = tables[0]
		binary_path = directory / f"{name}.bin"
		binary_path.write_bytes(data)
		object_path = directory / "tables.o"
		stack_note_path = directory / "empty"
		stack_note_path.write_bytes(b"")

		command = [
			*objcopy,
			"--input-target", "binary",
			"--output-target", bfd_target.file_format,
			"--binary-architecture", bfd_target.architecture,
			"--rename-section", f".data=.{name},alloc,load,readonly,data,contents",
			"--set-section-alignment", ".data=4", "--strip-all",
			"--add-section", f".note.GNU-stack={stack_note_path}",
		]

		for name, data in tables[1:]:
			table_path = directory / f"{name}.bin"
			table_path.write_bytes(data)
			command.extend(["--add-section", f".{name}={table_path}"])
			command.extend(["--set-section-flags", f".{name}=alloc,load,readonly,data,contents"])

		for name, data in tables:
			command.extend(["--add-symbol", f"{name}=.{name}:0,global,object"])
			command.extend(["--add-symbol", f"{name}_end=.{name}:{len(data)},global"])

		run_tool([*command, str(binary_path), str(object_path)])
		os.replace(object_path, output)

def main():
	parser = argparse.ArgumentParser(description="Generate a linkable kernel debug-symbol object using GNU binutils.")
	parser.add_argument("kernel_exec", help="kernel ELF binary")
	parser.add_argument("base_addr", type=lambda value: int(value, 0), help="kernel base address")
	parser.add_argument("source_cwd", help="build directory prefix to strip from source filenames")
	parser.add_argument("output_file", help="output object")
	parser.add_argument("--nm", type=tool_command, default=os.environ.get("NM", "nm"), help="GNU nm command (default: $NM or nm)")
	parser.add_argument("--objdump", type=tool_command, default=os.environ.get("OBJDUMP", "objdump"), help="GNU objdump command (default: $OBJDUMP or objdump)")
	parser.add_argument("--objcopy", type=tool_command, default=os.environ.get("OBJCOPY", "objcopy"), help="GNU objcopy command (default: $OBJCOPY or objcopy)")
	args = parser.parse_args()

	try:
		bfd_target = get_bfd_target(args.kernel_exec, args.objdump)
		symbols = collect_symbols(args.kernel_exec, args.base_addr, args.source_cwd, args.nm)
		tables = make_tables(symbols)
		write_object(tables, args.output_file, args.objcopy, bfd_target)
	except subprocess.CalledProcessError as error:
		parser.exit(1, f"{error.cmd[0]} failed: {error.stderr.strip()}\n")
	except (OSError, ValueError) as error:
		parser.exit(1, f"{error}\n")


if __name__ == "__main__":
	main()
