
import h5py # presents HDF5 files as numpy arrays
import numpy as np
#import statistics as stats

# Open stats file
f = h5py.File('zsim-ev.h5', 'r')

ds = f["stats"]["root"]

# ds[-1] is the end of the simulation. So final stats

cache_types = ['l1i', 'l1d', 'l2', 'l3']

print(f"{len(ds[-1]['l2']['hGETS'])} Cores")
print(f"{ds[-1]['l2'].dtype.names}")
print(f"{ds[-1]['l1i'].dtype.names}")


for c in cache_types:
    hits = 0
    if c == 'l1i' or c == 'l1d':
        hits = sum(ds[-1][c]['hGETS']) + sum(ds[-1][c]['hGETX']) + sum(ds[-1][c]['fhGETX']) + sum(ds[-1][c]['fhGETS'])
    else:
       hits = sum(ds[-1][c]['hGETS']) + sum(ds[-1][c]['hGETX'])

    print("Sum {} cache hits : {}".format(c, hits))
    miss = sum(ds[-1][c]['mGETS'])
    print("Sum {} cache misses : {}".format(c, miss))

mpki = (sum(ds[-1]['l1i']['mGETS']) * 1000) / sum(ds[-1]["beefy"]['instrs'])
print(f"l1i MPKI {mpki}")
#for k in ds[-1]['l1d'].dtype.names:
#   print(k)

print(f" l1d filtered GETS {sum(ds[-1]['l1d']['fhGETS'])}")
print(f" l1d filtered GETX {sum(ds[-1]['l1d']['fhGETX'])}")
#print(f" l1d filtered Miss {sum(ds[-1]['l1d']['fmGET'])}")
#print(f" l1d filtered Miss cycles {sum(ds[-1]['l1d']['fmGETLat'])}")

print(f" l1i filtered GETS {sum(ds[-1]['l1i']['fhGETS'])}")
print(f" l1i filtered GETX {sum(ds[-1]['l1i']['fhGETX'])}")
#print(f" l1i filtered Miss {sum(ds[-1]['l1i']['fmGET'])}")
#print(f" l1i filtered Miss cycles {sum(ds[-1]['l1i']['fmGETLat'])}")

instr_count = sum(ds[-1]["beefy"]['instrs'])
cycle_count = sum(ds[-1]["beefy"]['cycles'])
print("Total instruction count: {}".format(instr_count))
print("Total cycles count: {}".format(cycle_count))

print("Cumulative IPC: {}".format(instr_count / cycle_count))


#for k in f["stats"]["root"][-1]["westmere"].dtype.names:
#   print(k)

#print(f["stats"]["root"][-1]["westmere"]["cycles"])
#print(sum(f["stats"]["root"][-1]["westmere"]["instrs"]))

