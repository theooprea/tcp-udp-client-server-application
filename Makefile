build: server.cpp subscriber.cpp
	g++ server.cpp -o server
	g++ subscriber.cpp -o subscriber
server: server.cpp
	g++ server.cpp -o server
subscriber: subscriber.cpp
	g++ subscriber.cpp -o subscriber
clean: server subscriber
	rm server subscriber
