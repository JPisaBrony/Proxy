all: proxy

proxy:
	gcc main.c -g -lpthread -o proxy

clean:
	rm proxy
