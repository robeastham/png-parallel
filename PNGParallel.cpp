//============================================================================
// Name			: PNGParallel.cpp
// Author		: Pascal Beyeler
// Version		: 1.0
// Copyright	: This content is released under the http://www.opensource.org/licenses/mit-license.php MIT License.
// Description	: Parallelized PNG encoder with OpenMP
//============================================================================


#include "PNGParallel.h"


/**
 * Function used internally in libpng to write to an output file
 *
 * @param	pngPtr
 * @param	data
 * @param	length
 */

void pngWrite(png_structp pngPtr, png_bytep data, png_size_t length) {
	ofstream* file = reinterpret_cast<ofstream*>(png_get_io_ptr(pngPtr));
	file->write(reinterpret_cast<char*>(data), length);
	if (file->bad()) {
		cout << reinterpret_cast<char*>(data) << endl << endl << length << endl << file->badbit << endl;
		png_error(pngPtr, "Write error");
	}
}


/**
 * Function used internally in libpng to flush the output buffer to a file
 *
 * @param	pngPtr
 */

void pngFlush(png_structp pngPtr) {
	ofstream* file = reinterpret_cast<ofstream*>(png_get_io_ptr(pngPtr));
	file->flush();
}


/**
 * Constructor with ImageMagick image representation
 * @param	inputFile
 */

PNGParallel::PNGParallel(Image::Image &inputFile)
	:InputFile(&inputFile) {

	//default settings
	CompressionLevel = 9;
	NumThreads = 2;

}


/**
 * Set number of threads used for the compression
 *
 * @param	numThreads		number of threads
 */

void PNGParallel::setNumThreads(int numThreads) {

	NumThreads = numThreads;
	omp_set_num_threads(numThreads);

}


/**
 * Set the zlib compression level used for deflate
 *
 * @param	compressionLevel		level of compression 1-9
 */

void PNGParallel::setCompressionLevel(int compressionLevel) {

	CompressionLevel = compressionLevel;

}


/**
 * Filter a pixel row to optimize deflate compression
 * Note: Filtering is disabled in this implementation.
 *
 * @param	pixels		pointer to the pixels in memory
 * @param	rowLength	amount of pixels per row
 * @return	filtered row
 */

PixelPacket* PNGParallel::filterRow(PixelPacket* pixels, int rowLength) {

	unsigned short colorTmp;
	char filterByte = 0;

	//Transformations
	for (int j = 0; j < rowLength; j++) {
		//swap red and blue in BGRA mode
		colorTmp = pixels->red;
		pixels->red = pixels->blue;
		pixels->blue = colorTmp;

		//change opacity
		pixels->opacity = TransparentOpacity;
		pixels += 1;
	}
	pixels -= rowLength;

	//Add filter byte 0 to disable row filtering
	PixelPacket* filteredRow = reinterpret_cast<PixelPacket*>(malloc(rowLength * COLOR_FORMAT_BPP + 1));
	memcpy(&(reinterpret_cast<char*>(filteredRow)[0]), &filterByte, 1);
	memcpy(&(reinterpret_cast<char*>(filteredRow)[1]), pixels, COLOR_FORMAT_BPP * rowLength);

	return filteredRow;

}


/**
 * Filter all pixel rows to optimize the deflate compression
 * Note: Filtering is disabled in this implementation.
 *
 * @param	pixels		pointer to the pixels in memory
 * @param	rowLength	amount of pixels per row
 * @return	filtered rows
 */

PixelPacket* PNGParallel::filterRows(PixelPacket* pixels, int rows, int rowLength) {

	int bytesPerRow = (COLOR_FORMAT_BPP * rowLength + 1);
	PixelPacket* filteredRows = reinterpret_cast<PixelPacket*>(malloc(rows * bytesPerRow));
	PixelPacket* filteredRow;

	for(int row = 0; row < rows; row++) {
		filteredRow = this->filterRow(pixels, rowLength);
		memcpy(reinterpret_cast<char*>(filteredRows), filteredRow, bytesPerRow);
		pixels += rowLength;
		filteredRows = reinterpret_cast<PixelPacket*>(reinterpret_cast<char*>(filteredRows) + bytesPerRow);
	}

	filteredRows = reinterpret_cast<PixelPacket*>(reinterpret_cast<char*>(filteredRows) - (bytesPerRow * rows));

	return filteredRows;

}


/**
 * Compress the loaded image to the specified output file
 *
 * @param	outputFile	output file for the generated PNG file
 */

void PNGParallel::compress(ofstream &outputFile) {

	//Init PNG write struct
	png_structp pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!pngPtr) {
		cout << "PNG write struct could not be initialized" << endl;
		return;
	}

	//Init PNG info struct
	png_infop infoPtr = png_create_info_struct(pngPtr);
	if (!infoPtr) {
		png_destroy_write_struct(&pngPtr, (png_infopp) NULL);
		cout << "PNG info struct could not be initialized" << endl;
		return;
	}

	//Tell the pnglib where to save the image
	png_set_write_fn(pngPtr, &outputFile, pngWrite, pngFlush);

	//Change from RGB to BGR as Magick++ uses this format
	png_set_bgr(pngPtr);

	//For the sake of simplicity we do not apply any filters to a scanline
	png_set_filter(pngPtr, 0, PNG_FILTER_NONE);

	//Write IHDR chunk
	Geometry::Geometry ig = this->InputFile->size();
	int height = ig.height();
	int width = ig.width();
	png_set_IHDR(pngPtr, infoPtr, width, height, QuantumDepth, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_BASE);

	//Write the file header information.
	png_write_info(pngPtr, infoPtr);

	//Init vars used for compression
	int totalDeflateOutputSize = 0;
	unsigned long adler32Combined = 0L;
	z_stream zStreams[NumThreads];
	size_t deflateOutputSize[NumThreads];
	char *deflateOutput[NumThreads];

	#pragma omp parallel for default(shared)
	for (int threadNum = 0; threadNum < NumThreads; threadNum++) {

		int ret, flush, row, stopAtRow;
		unsigned int have;
		const int chunkSize = 16384;
		unsigned char output_buffer[chunkSize];

		//Calculate which lines have to be handled by this thread
		row = threadNum * static_cast<int>(ceil(static_cast<double>(height) / static_cast<double>(NumThreads)));
		stopAtRow = static_cast<int>(ceil(static_cast<double>(height) / static_cast<double>(NumThreads))) * (threadNum + 1);
		stopAtRow = stopAtRow > height ? height : stopAtRow;

		//Load all pixel data
		PixelPacket* pixels = InputFile->getPixels(0, row, width, stopAtRow - row);
		FILE *deflate_stream = open_memstream(&deflateOutput[threadNum], &deflateOutputSize[threadNum]);

		//Allocate deflate state
		zStreams[threadNum].zalloc = Z_NULL;
		zStreams[threadNum].zfree = Z_NULL;
		zStreams[threadNum].opaque = Z_NULL;
		if (deflateInit(&zStreams[threadNum], 9) != Z_OK) {
			cout << "Not enough memory for compression" << endl;
		}

		//Add the filter byte and reorder the RGB values
		PixelPacket *filteredRows = this->filterRows(pixels, stopAtRow - row, width);

		//Let's compress line by line so the input buffer is the number of bytes of one pixel row plus the filter byte
		zStreams[threadNum].avail_in = (COLOR_FORMAT_BPP * width + 1) * (stopAtRow - row);
		zStreams[threadNum].avail_in += stopAtRow == height ? 1 : 0;

		//Finish the stream if it's the last pixel row
		flush = stopAtRow == height ? Z_FINISH : Z_SYNC_FLUSH;
		zStreams[threadNum].next_in = reinterpret_cast<Bytef*>(filteredRows);

		//Compress the image data with deflate
		do {
			zStreams[threadNum].avail_out = chunkSize;
			zStreams[threadNum].next_out = output_buffer;
			ret = deflate(&zStreams[threadNum], flush);
			have = chunkSize - zStreams[threadNum].avail_out;
			fwrite(&output_buffer, 1, have, deflate_stream);
		} while (zStreams[threadNum].avail_out == 0);

		fclose(deflate_stream);
		totalDeflateOutputSize += deflateOutputSize[threadNum];

		//Calculate the combined adler32 checksum
		int input_length = (stopAtRow - (threadNum * (height / NumThreads))) * (COLOR_FORMAT_BPP * width + 1);
		adler32Combined = adler32_combine(adler32Combined, zStreams[threadNum].adler, input_length);

		//Finish deflate process
		(void) deflateEnd(&zStreams[threadNum]);

	}

	//Concatenate the zStreams
	png_byte *idatData = new png_byte[totalDeflateOutputSize];
	for (int i = 0; i < NumThreads; i++) {
		if(i == 0) {
			memcpy(idatData, deflateOutput[i], deflateOutputSize[i]);
			idatData += deflateOutputSize[i];
		} else {
			//strip the zlib stream header
			memcpy(idatData, deflateOutput[i] + 2, deflateOutputSize[i] - 2);
			idatData += (deflateOutputSize[i] - 2);
			totalDeflateOutputSize -= 2;
		}
	}

	//Add the combined adler32 checksum
	idatData -= sizeof(adler32Combined);
	memcpy(idatData, &adler32Combined, sizeof(adler32Combined));
	idatData -= (totalDeflateOutputSize - sizeof(adler32Combined));

	//We have to tell libpng that an IDAT was written to the file
	pngPtr->mode |= PNG_HAVE_IDAT;

	//Create an IDAT chunk
	png_unknown_chunk idatChunks[1];
	strcpy((png_charp) idatChunks[0].name, "IDAT");
	idatChunks[0].data = idatData;
	idatChunks[0].size = totalDeflateOutputSize;
	idatChunks[0].location = PNG_AFTER_IDAT;
	pngPtr->flags |= 0x10000L; //PNG_FLAG_KEEP_UNSAFE_CHUNKS
	png_set_unknown_chunks(pngPtr, infoPtr, idatChunks, 1);
	png_set_unknown_chunk_location(pngPtr, infoPtr, 0, PNG_AFTER_IDAT);

	//Write the rest of the file
	png_write_end(pngPtr, infoPtr);

	//Cleanup
	png_destroy_write_struct(&pngPtr, &infoPtr);
	delete(idatData);


}


PNGParallel::~PNGParallel() {

}

