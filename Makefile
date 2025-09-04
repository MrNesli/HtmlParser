CC=gcc
CFLAGS=-g -ljson-c

html_to_json: html_to_json.c
	$(CC) html_to_json.c $(CFLAGS) -o html_to_json
