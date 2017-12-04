all: clean objs my_tests

objs:
	mkdir objs

objs/%.o: %.c
	gcc -c $< -o $@

my_tests: objs/my_tests.o myalloc
	gcc objs/my_tests.o -o my_tests -L. -lmyalloc -lpthread

myalloc : objs/myalloc.o
	ar -cvr libmyalloc.a objs/myalloc.o
	ar -t libmyalloc.a

clean:
	/bin/rm -rf *.o my_tests libmyalloc.a objs
