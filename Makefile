all:
	gcc  -shared rwpng.c imagequant.c -limagequant -llua -lpng -O3 -fpic -g -fPIC -I/usr/local/include -o imagequant.so
