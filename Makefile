dynamic:
	gcc  -shared rwpng.c luaquant.c -limagequant -llua -lpng -O3 -fpic -g -fPIC -I/usr/local/include -o libluaquant.so
all:
	gcc  -c rwpng.c luaquant.c -limagequant -lpng -O3 -I/usr/local/include
	ar crv libluaquant.a *.o
