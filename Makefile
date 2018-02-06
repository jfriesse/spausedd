CFLAGS += -Wall -Wshadow -Wp,-D_FORTIFY_SOURCE=2 -g -O
LDFLAGS += -lrt
PROGRAM_NAME = spausedd

$(PROGRAM_NAME): spausedd.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

all: $(PROGRAM_NAME)

clean:
	rm -f $(PROGRAM_NAME)
