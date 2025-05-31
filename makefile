CC=gcc
OUT=build/video_trimmer
IN=src/main.c
LIBS=-lavformat -lavcodec -lavutil
all:
	$(CC) $(IN) $(LIBS) -o $(OUT) 