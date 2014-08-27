all:
	gcc  -shared imagequant.c -limagequant -llua -O3 -fpic -g -fPIC -I/usr/local/include -o imagequant.so
