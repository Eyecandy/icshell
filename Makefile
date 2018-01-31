all: ICshell.c 
	gcc -g -Wall -o icsh ICshell.c

clean:
	$(RM) icsh