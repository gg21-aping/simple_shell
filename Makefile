CC = gcc
CFLAGS = -g -Wall -O
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

TARGET = ish

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) $(TARGET) $(OBJS) *~