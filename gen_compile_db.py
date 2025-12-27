#!/usr/bin/env python3
# Generates a compile_commands.json file for the kernel

import os
import pathlib
import json

path = pathlib.Path(__file__).resolve()
kernel_source = path.parent / 'kernel-src'
cflags = []

with open(kernel_source / 'Makefile', 'r') as f:
    lines = f.readlines()

    def find_var(name):
        line = next(x for x in lines if x.startswith(f'{name}='))
        value = line.strip().split('=', 1)[1].strip()

        # Handle comment
        if '#' in value:
            value = value.split('#', 1)[0].strip()

        return value

    cflags_var = find_var('CFLAGS').split()
    uacpiopts_var = find_var('UACPIOPTS').split()
    printfopts_var = find_var('PRINTFOPTS').split()
    kernelconfig_var = find_var('KERNELCONFIG').split()
    flantermopts_var = find_var('FLANTERMOPTS').split()

    incdir = kernel_source / 'include'
    archincdir = kernel_source / 'include' / 'x86-64'
    flantermincdir = kernel_source / 'flanterm'
    uacpiincdir = kernel_source / 'io' / 'acpi' / 'uacpi' / 'include'

    for f in cflags_var:
        if f == '"$(UACPIINCDIR)"':
            cflags.append(str(uacpiincdir))
        elif f == '"$(INCDIR)"':
            cflags.append(str(incdir))
        elif f == '"$(ARCHINCDIR)"':
            cflags.append(str(archincdir))
        elif f == '"$(FLANTERMINCDIR)"':
            cflags.append(str(flantermincdir))
        elif f == '$(UACPIOPTS)':
            cflags.extend(uacpiopts_var)
        elif f == '$(PRINTFOPTS)':
            cflags.extend(printfopts_var)
        elif f == '$(KERNELCONFIG)':
            cflags.extend(kernelconfig_var)
        elif f == '$(FLANTERMOPTS)':
            cflags.extend(flantermopts_var)
        else:
            cflags.append(f)

csources = [
    os.path.join(root, f)
    for root, _, files in os.walk(kernel_source)
    for f in files
    if f.endswith('.c') and '/uacpi/tests/' not in os.path.join(root, f)
]

compile_commands = []

for src in csources:
    command = {
        'directory': str(kernel_source),
        'arguments': ['gcc', *cflags, '-c', src],
        'file': src
    }
    compile_commands.append(command)

with open(kernel_source / 'compile_commands.json', 'w') as f:
    json.dump(compile_commands, f, indent=2)
