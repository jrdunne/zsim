
#!/usr/bin/pytho

import h5py # presents HDF5 files as numpy arrays
import numpy as np
#import statistics as stats

# Open stats file
f = h5py.File('zsim-ev.h5', 'r')

ds = f["stats"]["root"]

# ds[-1] is the end of the simulation. So final stats

cache_types = ['l1i', 'l1d', 'l2', 'l3']

print(f"{len(ds[-1]['l2']['hGETS'])} Cores")
#print(f"{ds[-1]['l2']['hGETS']}")

for c in cache_types:
    hits = sum(ds[-1][c]['hGETS']) + sum(ds[-1][c]['hGETX'])
    print("Sum {} cache hits : {}".format(c, hits))
    miss = sum(ds[-1][c]['mGETS'])
    print("Sum {} cache misses : {}".format(c, miss))

for k in ds[-1]['l1d'].dtype.names:
   print(k)

print(ds[-1]['l1d']['fhGETS'])
print(ds[-1]['l1d']['fhGETX'])

instr_count = ds[-1]["beefy"]['instrs'][0]
cycle_count = ds[-1]["beefy"]['cycles'][0]
print("Total instruction count: {}".format(instr_count))
print("Total cycles count: {}".format(cycle_count))

print("Cumulative IPC: {}".format(instr_count / cycle_count))


#for k in f["stats"]["root"][-1]["westmere"].dtype.names:
#   print(k)

#print(f["stats"]["root"][-1]["westmere"]["cycles"])
#print(sum(f["stats"]["root"][-1]["westmere"]["instrs"]))

