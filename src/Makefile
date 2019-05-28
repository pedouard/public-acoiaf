TARGET=exec-bot
LIB_TARGET=libcbot.so
CFLAGS=-Wall
ENDFLAG=-lm

.PHONY: all clean

all: $(TARGET) $(LIB_TARGET)

$(TARGET):
	gcc -o $(TARGET) cbotmodule.c $(ENDFLAG)

$(LIB_TARGET):
	gcc -shared -o $(LIB_TARGET) -fPIC cbotmodule.c $(ENDFLAG)

clean:
	rm -f $(TARGET) $(LIB_TARGET)



