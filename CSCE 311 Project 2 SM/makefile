GPP = g++ -Wall -std=c++11

LIBS = -lrt -lpthread

A = main.o

a: $A
	$(GPP) -o a.out $A $(LIBS) 

main.o: main.cpp
	$(GPP) -c main.cpp

clean: 
	rm *.o a.out
