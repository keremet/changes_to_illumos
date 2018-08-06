all: iwm

iwm: if_iwm.o
	/usr/ccs/bin/ld -r  -dy -Nmisc/mac -Nmisc/net80211 -Ndrv/random -Ndrv/ip -o $@ if_iwm.o

if_iwm.o: if_iwm.c
	cc -I/export/home/moscow/illumos-gate/usr/src/uts/common -D_KERNEL -m64 -xarch=sse2a -xmodel=kernel -c if_iwm.c


