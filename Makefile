EXEC = powermeter

CFLAGS += -std=gnu99
LDFLAGS += 

CC = armv7a-hardfloat-linux-gnueabi-gcc

SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -fv *.o

mrproper: clean
	rm -fv $(EXEC)
