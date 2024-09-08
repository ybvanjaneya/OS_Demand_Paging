all: master sched process mmu
	
master: master.c
	gcc master.c -o master
sched: sched.c
	gcc sched.c -o sched
process: process.c
	gcc process.c -o process
mmu: mmu.c
	gcc mmu.c -o mmu
clean:
	rm mmu master sched process result.txt