objects := $(patsubst %.c,%.o,$(wildcard *.c))
OBJPATH=$(OBJDIR)/arch/$(TARGET)/boot
export

all: $(objects)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
	mkdir -p $(OBJPATH) 
	cp $@ $(OBJPATH)/$@
