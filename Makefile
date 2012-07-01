CXXFLAGS =	-O3 -g -Wall -fmessage-length=0 -fopenmp -I/usr/include/ImageMagick

OBJS =		pngencoder.o PNGParallel.o

LIBS =	 -lboost_program_options -lMagick++ -lpng

TARGET =	pngencoder

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS)

all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
