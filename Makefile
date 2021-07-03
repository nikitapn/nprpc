# libnprpc.so
# nprpcst
# npnameserver

CXX = g++
CXXVER = c++20

CXXFLAGS = -std=$(CXXVER) -O2 -Wall

SRCS = $(shell find src/ -name '*.cpp') 
OBJS = $(addprefix obj/,$(addsuffix .o, $(basename $(notdir $(SRCS)))))

OUT_BIN = ../build/bin
OUT_LIB = ../build/lib

$(OUT_BIN)/npnameserver: $(OUT_LIB)/libnprpc.so $(OUT_LIB)/nprpcst.o obj/npnameserver.o
	$(CXX) -lpthread -L$(OUT_LIB) -lnprpc $(OUT_LIB)/nprpcst.o obj/npnameserver.o -o $@

obj/npnameserver.o: npnameserver/npnameserver.cpp
	$(CXX) $(CXXFLAGS) -I./include -c -o $@ $<

$(OUT_LIB)/nprpcst.o: include/nprpc/nprpc_nameserver.cpp
	$(CXX) $(CXXFLAGS) -fPIC -I./include -c -o $@ $<

$(OUT_LIB)/libnprpc.so: $(OBJS)
	$(CXX) $(OBJS) -lpthread /usr/lib/libboost_system.a -shared -o $@

obj/%.o: src/%.cpp | obj
	$(CXX) $(CXXFLAGS) -fPIC -I./include -c -o $@ $<

obj:
	mkdir -p obj

install:
	cp lib/libnprpc.so /usr/local/lib
	cp bin/npnameserver /usr/local/bin

clean:
	rm -rf obj/

.PHONY: install clean
