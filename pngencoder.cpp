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
#include <time.h>
#include <omp.h>
#include <zlib.h>

using namespace std;
using namespace Magick;
namespace po = boost::program_options;


//Depending on the magick++ build, different amount of bytes for the pixel representation is used
#if QuantumDepth==8
	int COLOR_FORMAT_BPP = 4;
#elif QuantumDepth==16
	int COLOR_FORMAT_BPP = 8;
#endif

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

PixelPacket* png_filter_row(PixelPacket* pixels, int row_length) {

	unsigned short color_tmp;
	char filter_byte = 0;

	//Transformations
	for (int j = 0; j < row_length; j++) {
		//swap red and blue in BGRA mode
		color_tmp = pixels->red;
		pixels->red = pixels->blue;
		pixels->blue = color_tmp;

		//change opacity
		pixels->opacity = TransparentOpacity;
		pixels += 1;
	}
	pixels -= row_length;

	//Add filter byte 0 to disable row filtering
	PixelPacket* filtered_row = (PixelPacket*) malloc(row_length * COLOR_FORMAT_BPP + 1);
	memcpy(&((char*)filtered_row)[0], &filter_byte, 1);
	memcpy(&((char*)filtered_row)[1], pixels, COLOR_FORMAT_BPP * row_length);

	return filtered_row;

}

PixelPacket* png_filter_rows(PixelPacket* pixels, int rows, int row_length) {

	int bytes_per_row = (COLOR_FORMAT_BPP * row_length + 1);
	PixelPacket* filtered_rows = (PixelPacket*) malloc(rows * bytes_per_row);
	PixelPacket* filtered_row;

	for(int row = 0; row < rows; row++) {
		filtered_row = png_filter_row(pixels, row_length);
		memcpy((char*)filtered_rows, filtered_row, bytes_per_row);
		pixels += row_length;
		filtered_rows = (PixelPacket*)((char*)filtered_rows + bytes_per_row);
	}

	filtered_rows = (PixelPacket*)((char*)filtered_rows - (bytes_per_row * rows));

	return filtered_rows;

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

	//For the sake of simplicity we do not apply any filters to a scanline
	png_set_filter(png_ptr, 0, PNG_FILTER_NONE);

	//Write IHDR chunk
	Geometry::Geometry ig = input_file.size();
	int height = ig.height();
	int width = ig.width();
	png_set_IHDR(png_ptr, info_ptr, width, height, QuantumDepth, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_BASE);

	//Write the file header information.
	png_write_info(png_ptr, info_ptr);

	//Init vars used for compression
	int total_deflate_output_size = 0;
	unsigned long adler32_combined = 0L;
	const int num_threads = 4;
	omp_set_num_threads(num_threads);
	z_stream z_streams[num_threads];
	size_t deflate_output_size[num_threads];
	char *deflate_output[num_threads];

	#pragma omp parallel for default(shared)
	for (int thread_num = 0; thread_num < num_threads; thread_num++) {

		int ret, flush, row, stop_at_row;
		unsigned int have;
		const int chunk_size = 128;
		unsigned char output_buffer[chunk_size];

		//Calculate which lines have to be handled by this thread
		row = thread_num * (int)ceil((double)height / (double)num_threads);
		stop_at_row = (int)ceil((double)height / (double)num_threads) * (thread_num + 1);
		stop_at_row = stop_at_row > height ? height : stop_at_row;

		//Load all pixel data
		PixelPacket* pixels = input_file.getPixels(0, row, width, stop_at_row - row);
		FILE *deflate_stream = open_memstream(&deflate_output[thread_num], &deflate_output_size[thread_num]);

		//Allocate deflate state
		z_streams[thread_num].zalloc = Z_NULL;
		z_streams[thread_num].zfree = Z_NULL;
		z_streams[thread_num].opaque = Z_NULL;
		if (deflateInit(&z_streams[thread_num], 9) != Z_OK) {
			cout << "Not enough memory for compression" << endl;
		}

		//Add the filter byte and reorder the RGB values
		PixelPacket *filtered_rows = png_filter_rows(pixels, stop_at_row - row, width);

		//Let's compress line by line so the input buffer is the number of bytes of one pixel row plus the filter byte
		z_streams[thread_num].avail_in = (COLOR_FORMAT_BPP * width + 1) * (stop_at_row - row);
		z_streams[thread_num].avail_in += stop_at_row == height ? 1 : 0;

		//Finish the stream if it's the last pixel row
		flush = stop_at_row == height ? Z_FINISH : Z_SYNC_FLUSH;
		z_streams[thread_num].next_in = (Bytef*)filtered_rows;

		time_t begin = clock();

		//Compress the image data with deflate
		do {
			z_streams[thread_num].avail_out = chunk_size;
			z_streams[thread_num].next_out = output_buffer;
			ret = deflate(&z_streams[thread_num], flush);
			have = chunk_size - z_streams[thread_num].avail_out;
			fwrite(&output_buffer, 1, have, deflate_stream);
		} while (z_streams[thread_num].avail_out == 0);

		time_t end = clock();
		cout << "Compression of row " << row << " to " << stop_at_row << " in thread " << omp_get_thread_num() <<  ": " << double(diffclock(end, begin)) << " ms" << endl;

		fclose(deflate_stream);
		total_deflate_output_size += deflate_output_size[thread_num];

		//Calculate the combined adler32 checksum
		int input_length = (stop_at_row - (thread_num * (height / num_threads))) * (COLOR_FORMAT_BPP * width + 1);
		adler32_combined = adler32_combine(adler32_combined, z_streams[thread_num].adler, input_length);

		//Finish deflate process
		(void) deflateEnd(&z_streams[thread_num]);

	}

	//Concatenate the z_streams
	png_byte *IDAT_data = new png_byte[total_deflate_output_size];
	for (int i = 0; i < num_threads; i++) {
		if(i == 0) {
			memcpy(IDAT_data, deflate_output[i], deflate_output_size[i]);
			IDAT_data += deflate_output_size[i];
		} else {
			//strip the zlib stream header
			memcpy(IDAT_data, deflate_output[i] + 2, deflate_output_size[i] - 2);
			IDAT_data += (deflate_output_size[i] - 2);
			total_deflate_output_size -= 2;
		}
	}

	//Add the combined adler32 checksum
	IDAT_data -= sizeof(adler32_combined);
	memcpy(IDAT_data, &adler32_combined, sizeof(adler32_combined));
	IDAT_data -= (total_deflate_output_size - sizeof(adler32_combined));

	//We have to tell libpng that an IDAT was written to the file
	png_ptr->mode |= PNG_HAVE_IDAT;

	//Create an IDAT chunk
	png_unknown_chunk idat_chunks[1];
	strcpy((png_charp) idat_chunks[0].name, "IDAT");
	idat_chunks[0].data = IDAT_data;
	idat_chunks[0].size = total_deflate_output_size;
	idat_chunks[0].location = PNG_AFTER_IDAT;
	png_ptr->flags |= 0x10000L; //PNG_FLAG_KEEP_UNSAFE_CHUNKS
	png_set_unknown_chunks(png_ptr, info_ptr, idat_chunks, 1);
	png_set_unknown_chunk_location(png_ptr, info_ptr, 0, PNG_AFTER_IDAT);

	//Write the rest of the file
	png_write_end(png_ptr, info_ptr);

	//Cleanup
	png_destroy_write_struct(&png_ptr, &info_ptr);

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
