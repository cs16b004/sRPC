CXX=g++

INC=-I.. -I. -I../rpc -I../base -I../rpc/dpdk_transport

LIB_DPDK=$(shell pkg-config --libs libdpdk) -lrte_net_qdma
CFLAG_DPDK=$(shell pkg-config --cflags libdpdk)
LIB=-Llib -lrpc -lbase -lpthread -lyaml-cpp $(LIB_DPDK)
CXXFLAGS=-std=c++17 -g -Wall $(CFLAG_DPDK)

.PHONY: all
all: clean bin/server bin/client bin/client_share_n bin/client_share_one

debug: CXXFLAGS += -g3 -O0 -DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS
debug: all

stat: CXXFLAGS += -g3 -O0 -DRPC_STATISTICS
stat: all 

dpdk: CXXFLAGS += -g3 -O0  -DDPDK -DTRANSPORT_STATS -DRPC_STATISTICS
dpdk: all

.PHONY: clean
clean:
	rm -f counter.h
	rm -rf lib
	rm -f bin/client
	rm -f bin/client_share_n
	rm -f bin/client_share_one
	rm -f bin/server
	rm -f *.o
	rm -f a.out
	
	mkdir lib

counter.hpp: ../bin/rpcgen counter.rpc
	../bin/rpcgen counter.rpc

bin/server: s.cpp counter.hpp lib/librpc.a lib/libbase.a
	${CXX} ${CXXFLAGS} ${INC} s.cpp ${LIB} -o bin/server

bin/client: c.cpp counter.hpp lib/librpc.a lib/libbase.a
	${CXX} ${CXXFLAGS} ${INC} c.cpp ${LIB} -o bin/client

bin/client_share_n: c_share_n.cpp counter.hpp lib/librpc.a lib/libbase.a
	${CXX} ${CXXFLAGS} ${INC} c_share_n.cpp ${LIB} -o bin/client_share_n

bin/client_share_one: c_share_one.cpp counter.hpp lib/librpc.a lib/libbase.a
	${CXX} ${CXXFLAGS} ${INC} c_share_one.cpp ${LIB} -o bin/client_share_one

lib/librpc.a: client.o marshal.o polling.o server.o utils.o udpserver.o config.o transport.o
	ar rcs lib/librpc.a client.o marshal.o polling.o server.o utils.o udpserver.o config.o transport.o
	rm -f client.o marshal.o polling.o server.o utils.o udpserver.o

lib/libbase.a: basetypes.o logging.o misc.o strop.o threading.o unittest.o
	ar rcs lib/libbase.a basetypes.o logging.o misc.o strop.o threading.o unittest.o
	rm -f basetypes.o logging.o misc.o strop.o threading.o unittest.o

client.o: ../rpc/client.cpp ../rpc/client.hpp  transport.o
	${CXX} ${CXXFLAGS} ${INC} -c ../rpc/client.cpp transport.o -o client.o

marshal.o: ../misc/marshal.cpp ../misc/marshal.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../misc/marshal.cpp -o marshal.o

polling.o: ../rpc/polling.cpp ../rpc/polling.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../rpc/polling.cpp -o polling.o

transport.o: ../rpc/dpdk_transport/transport.cpp ../rpc/dpdk_transport/transport.hpp ../rpc/dpdk_transport/utils.hpp config.o marshal.o polling.o 
			${CXX} ${CXXFLAGS} ${INC} -c ../rpc/dpdk_transport/transport.cpp config.o polling.o -o transport.o

config.o: ../rpc/dpdk_transport/config.cpp ../rpc/dpdk_transport/config.hpp
		  ${CXX} ${CXXFLAGS} ${INC} -c ../rpc/dpdk_transport/config.cpp -o config.o

udpserver.o: ../rpc/UDPServer.cpp ../rpc/server.hpp transport.o config.o
	${CXX} ${CXXFLAGS} ${INC} -c ../rpc/UDPServer.cpp config.o transport.o -o udpserver.o
	echo "Compiling UDPServer"

server.o: ../rpc/server.cpp ../rpc/server.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../rpc/server.cpp -o server.o



utils.o: ../rpc/utils.cpp ../rpc/utils.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../rpc/utils.cpp -o utils.o

basetypes.o: ../base/basetypes.cpp ../base/basetypes.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../base/basetypes.cpp -o basetypes.o

logging.o: ../base/logging.cpp ../base/logging.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../base/logging.cpp -o logging.o

misc.o: ../base/misc.cpp ../base/misc.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../base/misc.cpp -o misc.o

strop.o: ../base/strop.cpp ../base/strop.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../base/strop.cpp -o strop.o

threading.o: ../base/threading.cpp ../base/threading.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../base/threading.cpp -o threading.o

unittest.o: ../base/unittest.cpp ../base/unittest.hpp
	${CXX} ${CXXFLAGS} ${INC} -c ../base/unittest.cpp -o unittest.o

