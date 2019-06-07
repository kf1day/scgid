default: scgid-dbg-target
prod: scgid-target

scgid-dbg-target: main.c
	gcc -Wall -g main.c -o scgid-dbg -DDEBUG_FLAG
	
scgid-target: main.c
	gcc -Wall -O2 main.c -o scgid
	strip scgid
