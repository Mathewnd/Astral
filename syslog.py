#!/usr/bin/env python3

# prints the last system call log of every thread
# (this is useful for debugging things like race conditions in userspace)

import sys

if len(sys.argv) != 2:
    print(sys.argv[0] + ": usage: " + sys.argv[0] + " INPUT_FILE")
    exit(1)

file = open(sys.argv[1], "r")

last_log = {} # key pid + "_" + tid

for line in file:
        if not " tid " in line:
            continue

        if not ": pid" in line:
            continue

        try:
            junk, useful_data = line.split("syscall: pid ")
        except:
            try:
                junk, useful_data = line.split("syscall return: pid ")
            except:
                print("pid:::", line)
                exit()
        try:
            pid, useful_data = useful_data.split(" tid ")
        except:
            print("pid2:::", line)
            exit()

        try:
            tid = useful_data.split(": ", 1)[0]
        except:
            print("tid:::", line)

        last_log[str(pid) + "_" + str(tid)] = line

for key in last_log:
    if " exit:" in last_log[key]:
        continuea
    print(key + ":", last_log[key], end="")
