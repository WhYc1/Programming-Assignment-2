sr: emulator.c sr.c
	gcc -Wall -ansi -pedantic -o sr emulator.c sr.c

gbn: emulator.c gbn.c
	gcc -Wall -ansi -pedantic -o gbn emulator.c gbn.c