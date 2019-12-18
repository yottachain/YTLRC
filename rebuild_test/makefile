cc=gcc
objects=gf256.o cm256.o ytlrc.o test.o
rebuild_test:$(objects)
	$(cc) -w -o rebuild_test $(objects) -lm
gf256.o:
	$(cc)  -c ../gf256.c -o gf256.o
cm256.o:
	$(cc)  -c ../cm256.c -o cm256.o
ytlrc.o:
	$(cc)  -lm -c ../YTLRC.c -o ytlrc.o 
test.o:
	$(cc) -c test.c -o test.o 

.PHONY:clean
clean :
	-rm -rf *.o  rebuild_test $(objects)

