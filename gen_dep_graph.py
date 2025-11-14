#!/usr/bin/env python3
# Generates a graph of the Kernel's init dependencies

import os
import subprocess
from matplotlib import pyplot as plt
import networkx as nx
from networkx.drawing.nx_agraph import graphviz_layout

path = os.path.abspath(__file__)
kernel_source = os.path.dirname(path) + '/kernel-src'

routine_dependencies = {}

lines = subprocess.Popen(['grep', '-R', 'INIT_ROUTINE_DEFINE(', kernel_source], stdout=subprocess.PIPE).stdout.readlines()

for line in lines:
    if b"#define" in line:
        continue

    line = line.strip()

    junk, line = line.split(b'(')

    line = line.replace(b';', b'').replace(b')', b'')

    if line.count(b',') > 2:
        name, flags, function, dependencies = line.split(b',', 3)
    else:
        # no dependencies
        name, flags, function = line.split(b',', 2)
        dependencies = b''

    name = name.strip().decode('utf-8')
    routine_dependencies[name] = []

    dependencies = dependencies.split(b',')

    for dep in dependencies:
        dep = dep.strip().decode('utf-8')
        if dep == '':
            continue

        routine_dependencies[name].insert(0, dep)

for routine in routine_dependencies:
    print("routine '", routine, "' has dependencies: ", end='', sep='')
    dependencies = routine_dependencies[routine]
    print(dependencies)

# plot

graph = nx.DiGraph()

graph.add_nodes_from(routine_dependencies.keys())

edges = []
for routine in routine_dependencies:
    for dep in routine_dependencies[routine]:
        edges.insert(0, (dep, routine))

graph.add_edges_from(edges)

plt.figure(figsize=(20, 16))

pos = graphviz_layout(graph, prog='dot')
nx.draw(graph, pos, with_labels=True, node_size=3000)
plt.title("Kernel init dependencies")
plt.savefig('graph_kernel_deps.png')

print("File saved in graph_kernel_deps.png")
