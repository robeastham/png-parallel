//============================================================================
// Name		: pngencoder.cpp
// Author	  : Pascal Beyeler
// Version	 :
// Copyright   : This content is released under the http://www.opensource.org/licenses/mit-license.php MIT License.
// Description : PNG encoder with OpenMP support
//============================================================================

typedef unsigned char byte;
typedef unsigned short u16;

#include <boost/program_options.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/streams/bufferstream.hpp>
#include <iostream>
#include <iterator>
#include <fstream>
#include <Magick++/Include.h>
#include <Magick++/Image.h>
#include <Magick++/Pixels.h>
#include <png.h>
#include <math.h>
#include <time.h>
#include <bits/time.h>
#include <omp.h>
#include <zlib.h>
#include <stdio.h>

using namespace std;
using namespace Magick;
using namespace boost::interprocess;
namespace po = boost::program_options;
#define PNG_FLAG_KEEP_UNSAFE_CHUNKS       0x10000L

double diffclock(clock_t clock1, clock_t clock2) {
	double diffticks = clock1 - clock2;
	double diffms = (diffticks * 1000) / CLOCKS_PER_SEC;
	return diffms;
}

void png_write(png_structp png_ptr, png_bytep data, png_size_t length) {
	ofstream* file = (ofstream*) png_get_io_ptr(png_ptr);
	file->write((char*) data, length);
	if (file->bad()) {
		cout << (char*) data << endl << endl << length << endl << file->badbit << endl;
		png_error(png_ptr, "Write error");
	}
}

void png_flush(png_structp png_ptr) {
	ofstream* file = (ofstream*) png_get_io_ptr(png_ptr);
	file->flush();
}

void png_encode(Image::Image &input_file, ofstream &output_file) {

	//Init PNG write struct
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		cout << "PNG write struct could not be initialized" << endl;
		return;
	}

	//Init PNG info struct
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct(&png_ptr, (png_infopp) NULL);
		cout << "PNG info struct could not be initialized" << endl;
		return;
	}

	//Tell the pnglib where to save the image
	png_set_write_fn(png_ptr, &output_file, png_write, png_flush);

	//Change from RGB to BGR as Magick++ uses this format
	png_set_bgr(png_ptr);

	png_set_filter(png_ptr, 0, PNG_FILTER_NONE);

	//Write IHDR chunk
	Geometry::Geometry ig = input_file.size();
	int height = ig.height();
	int width = ig.width();
	png_set_IHDR(png_ptr, info_ptr, width, height, QuantumDepth, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_BASE);

	//Depending on the magick++ build, different amount of bytes for the pixel representation is used
#if QuantumDepth==8
	int color_format_bpp = 4;
#elif QuantumDepth==16
	int color_format_bpp = 8;
#endif

	//Build rows
	PixelPacket* pixels = input_file.getPixels(0, 0, width, height);
	char** rows = (char**) png_malloc(png_ptr, (height * color_format_bpp * width) + height);
	PixelPacket* pixels_row;
	PixelPacket* pixels_op;
	short color_tmp;
	char filter_byte = 0;

	//#pragma omp parallel for private(pixels_row,pixels_op) shared(height,width,rows,pixels,color_format_bpp,png_ptr)
	for (int i = 0; i < height; i++) {
		pixels_row = pixels + i * width;
		pixels_op = pixels_row;
		for (int j = 0; j < width; j++) {

			//swap red and blue in BGRA mode
			color_tmp = pixels_op->red;
			pixels_op->red = pixels_op->blue;
			pixels_op->blue = color_tmp;

			//change opacity
			pixels_op->opacity = TransparentOpacity;
			pixels_op += 1;
		}
		rows[i] = (char*)png_malloc(png_ptr, color_format_bpp * width + 1);
		memcpy(&rows[i][0], &filter_byte, 1);
		memcpy(&rows[i][1], pixels_row, color_format_bpp * width);
	}
	png_set_rows(png_ptr, info_ptr, (png_bytepp) rows);

	//Write the file header information.
	png_write_info(png_ptr, info_ptr);

	//Compress the pixels
	int ret, flush, row;
	unsigned have;
	z_stream strm;
	stringstream compressed_image;
	int chunk_size = 16384;
	unsigned char output_buffer[chunk_size];
	size_t deflate_output_size;
	char *deflate_output;
	FILE *deflate_stream = open_memstream(&deflate_output, &deflate_output_size);

	//allocate deflate state
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	if (deflateInit(&strm, Z_SYNC_FLUSH) != Z_OK) {
		cout << "Not enough memory for compression" << endl;
	}

	//compress until there are no more pixels left to process
	row = 0;
	do {

		//let's compress line by line so the input buffer is the number of bytes of one pixel row plus the filter byte
		strm.avail_in = color_format_bpp * width + 1;

		//flush the stream if it's the last pixel row
		flush = row == (height - 1) ? Z_FINISH : Z_SYNC_FLUSH;
		strm.next_in = (Bytef*) rows[row];
		//run deflate() on input until output buffer not full, finish compression if all of source has been read in
		do {
			strm.avail_out = chunk_size;
			strm.next_out = output_buffer;
			ret = deflate(&strm, flush);
			assert(ret != Z_STREAM_ERROR);
			have = chunk_size - strm.avail_out;
			if (fwrite(&output_buffer, 1, have, deflate_stream) != have || ferror(deflate_stream)) {
				(void) deflateEnd(&strm);
			}
		} while (strm.avail_out == 0);
		assert(strm.avail_in == 0);

		row++;
	} while (flush != Z_FINISH);
	assert(ret == Z_STREAM_END);

	fflush(deflate_stream);

	cout << deflate_output_size << endl;
	//finish deflate process
	(void) deflateEnd(&strm);

	//We have to tell libpng that an IDAT was written to the file
	png_ptr->mode |= PNG_HAVE_IDAT;

	//Create an IDAT chunk
	png_unknown_chunk idat_chunks[1];
	strcpy((png_charp)idat_chunks[0].name, "IDAT");
	idat_chunks[0].data = (png_byte*)deflate_output;
	idat_chunks[0].size = deflate_output_size;
	idat_chunks[0].location = PNG_AFTER_IDAT;
	png_ptr->flags |= PNG_FLAG_KEEP_UNSAFE_CHUNKS;
	png_set_unknown_chunks(png_ptr, info_ptr, idat_chunks, 1);
	png_set_unknown_chunk_location(png_ptr, info_ptr, 0, PNG_AFTER_IDAT);


	//Write the rest of the file
	png_write_end(png_ptr, info_ptr);
	fclose(deflate_stream);
	//Cleanup
	png_destroy_write_struct(&png_ptr, &info_ptr);
	for (int i = 0; i < height; ++i) {
		png_free(png_ptr, rows[i]);
	}
	png_free(png_ptr, rows);

}

int main(int argc, char *argv[]) {

	clock_t begin = clock();

	Image::Image input_file;
	ofstream output_file;

	try {

		//Command line argument handling
		po::options_description desc("Allowed options");
		desc.add_options()("help", "produce help message")("input,i", po::value<string>(), "path to input file")(
				"output,o", po::value<string>(), "path to output file");

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("help")) {
			cout << desc << "\n";
			return 1;
		}

		if (vm.count("input")) {
			input_file.read(vm["input"].as<string>());

		} else {
			cout << "Input file was not set.\n";
			return 1;
		}

		if (vm.count("output")) {
			output_file.open(vm["output"].as<string>().c_str());
			if (!output_file.good()) {
				cout << "Output file could not be opened" << endl;
				return 1;
			}
		} else {
			cout << "Output file was not set.\n";
			return 1;
		}

		//Encode the file if everything is okay
		if (input_file.isValid() && output_file.good()) {
			png_encode(input_file, output_file);
		}
		output_file.close();

	} catch (exception& e) {
		cerr << "error: " << e.what() << "\n";
		return 1;
	} catch (...) {
		cerr << "Exception of unknown type!\n";
	}

	clock_t end = clock();
	cout << "Time elapsed: " << double(diffclock(end, begin)) << " ms" << endl;

	return 0;

}
