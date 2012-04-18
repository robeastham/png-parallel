CXXFLAGS =	-O2 -g -Wall -fmessage-length=0 -I/usr/include/ImageMagick

OBJS =		pngencoder.o

LIBS =	 -lboost_program_options -lMagick++ -lpng

TARGET =	pngencoder

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS)

all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
