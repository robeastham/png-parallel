//============================================================================
// Name			: PNGParallel.h
// Author		: Pascal Beyeler
// Version		: 1.0
// Copyright	: This content is released under the http://www.opensource.org/licenses/mit-license.php MIT License.
// Description	: Parallelized PNG encoder with OpenMP
//============================================================================


#ifndef PNGPARALLEL_H_
#define PNGPARALLEL_H_


#include <iostream>
#include <iterator>
#include <fstream>
#include <Magick++/Image.h>
#include <Magick++/Pixels.h>
#include <png.h>
#include <time.h>
#include <omp.h>
#include <zlib.h>
#include <math.h>

//Depending on the magick++ build, different amount of bytes for the pixel representation is used
#ifndef COLOR_FORMAT_BPP
	#if QuantumDepth==8
		#define COLOR_FORMAT_BPP 4
	#elif QuantumDepth==16
		#define COLOR_FORMAT_BPP 8
	#endif
#endif

using namespace std;
using namespace Magick;

class PNGParallel {
public:
	PNGParallel(Image::Image &inputFile);
	void setNumThreads(int threads);
	void setCompressionLevel(int compressionLevel);
	void compress(ofstream &outputFile);
	~PNGParallel();

private:
	PixelPacket* filterRow(PixelPacket* pixels, int rowLength);
	PixelPacket* filterRows(PixelPacket* pixels, int rows, int rowLength);

	int CompressionLevel;
	Image::Image *InputFile;
	int NumThreads;
};

#endif /* PNGPARALLEL_H_ */
