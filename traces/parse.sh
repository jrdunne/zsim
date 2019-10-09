#!/bin/bash

for (( i=0; i<39; i++ ))
do
/home/jrdunne/git-repos/standford-mast-zsim/traces/a.out /home/jrdunne/git-repos/renaissance/$i.perf.jit.txt.gz /home/jrdunne/git-repos/standford-mast-zsim/traces/finagle-http/$i.perf.jit.txt.trc
done
for (( i=40; i<75; i++ ))
do
/home/jrdunne/git-repos/standford-mast-zsim/traces/a.out /home/jrdunne/git-repos/renaissance/$i.perf.jit.txt.gz /home/jrdunne/git-repos/standford-mast-zsim/traces/finagle-http/$i.perf.jit.txt.trc 
done
for (( i=76; i<80; i++ ))                                                                                             do
/home/jrdunne/git-repos/standford-mast-zsim/traces/a.out /home/jrdunne/git-repos/renaissance/$i.perf.jit.txt.gz /home/jrdunne/git-repos/standford-mast-zsim/traces/finagle-http/$i.perf.jit.txt.trc
done
