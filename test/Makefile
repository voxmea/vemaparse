
ifeq ($(OS),Windows_NT)
vematest.exe: vematest.cpp ../include/vemaparse/lexer.h ../include/vemaparse/parser.h
	cl /EHsc /W3 vematest.cpp /I ../include /I c:/workspace/boost/1.54.0/include
else
vematest: vematest.cpp ../include/vemaparse/lexer.h ../include/vemaparse/parser.h
	clang -Wall -g -o vematest vematest.cpp -I ../include -std=c++11
endif
