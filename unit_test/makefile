cc=gcc
objects=gf256.o cm256.o ytlrc.o linuxmain.o
unit_test:$(objects)
	$(cc) -w -o unit_test $(objects) -lm
gf256.o:
	$(cc)  -c ../gf256.c -o gf256.o -DGF256_TARGET_MOBILE
cm256.o:
	$(cc)  -c ../cm256.c -o cm256.o
ytlrc.o:
	$(cc)  -lm -c ../YTLRC.c -o ytlrc.o 
linuxmain.o:
	$(cc) -c linuxmain.c -o linuxmain.o 

.PHONY:clean
clean :
	-rm -rf *.o  unit_test $(objects)

