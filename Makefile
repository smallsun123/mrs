CFLAGS = -O2 -Wall -Werror -Wshadow -Wextra -Wno-unused -Wfatal-errors

OBJS = sourcelist.o main.o

INCLUDE =

TARGET = mrs

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(INCLUDE) $(OBJS) $(CFLAGS)

main.o: main.c
	$(CC) -c $^ $(INCLUDE) $(CFLAGS)

sourcelist.o: list.h sourcelist.h sourcelist.c
	$(CC) -c $^ $(INCLUDE) $(CFLAGS)


clean:
	rm -f $(OBJS) $(TARGET) *.gch
