//============================================================================
// Name		: pngencoder.cpp
// Author	  : Pascal Beyeler
// Version	 :
// Copyright   : Copyright ist Aberglaube
// Description : PNG encoder with OpenMP support
//============================================================================

typedef unsigned char byte;
typedef unsigned short u16;

#include <boost/program_options.hpp>
#include <iostream>
#include <iterator>
#include <fstream>
#include <Magick++/Include.h>
#include <Magick++/Image.h>
#include <Magick++/Pixels.h>
#include <png.h>
#include <math.h>

using namespace std;
using namespace Magick;
namespace po = boost::program_options;

void png_write(png_structp png_ptr, png_bytep data, png_size_t length) {
	ofstream* file = (ofstream*) png_get_io_ptr(png_ptr);
	file->write((char*) data, length);
	if (file->bad()) {
		cout << (char*) data << endl << endl << length << endl << file->badbit
				<< endl;
		png_error(png_ptr, "Write error");
	}
}

void png_flush(png_structp png_ptr) {
	ofstream* file = (ofstream*) png_get_io_ptr(png_ptr);
	file->flush();
}

void encode(Image::Image &input_file, ofstream &output_file) {

	/*
	 * Init pnglib stuff to write the PNG file
	 */

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
			NULL, NULL);
	if (!png_ptr) {
		cout << "PNG write struct could not be initialized" << endl;
		return;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct(&png_ptr, (png_infopp) NULL);
		cout << "PNG info struct could not be initialized" << endl;
		return;
	}

	png_set_write_fn(png_ptr, &output_file, png_write, png_flush);

	//Write IHDR chunk
	Geometry::Geometry ig = input_file.size();
	int height = ig.height();
	int width = ig.width();

	png_set_IHDR(png_ptr, info_ptr, width, height, QuantumDepth,
			PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	//Depending on the magick++ build, different amount of bytes for pixel representation get used
#if QuantumDepth==8
	int color_format_bpp = 4;
#elif QuantumDepth==16
	int color_format_bpp = 8;
#endif

	PixelPacket* pixels = input_file.getPixels(0, 0, width, height);

	// build rows
	void** rows = (void**) png_malloc(png_ptr,
			height * color_format_bpp * width);
	PixelPacket* pixels_tmp;
	short color_tmp;
	for (int i = 0; i < height; ++i) {
		pixels_tmp = pixels;
		for (int j = 0; j < width; j++) {

			//swap red and blue in BGRA mode
#if MAGICK_PIXEL_BGRA
			color_tmp = pixels_tmp->red;
			pixels_tmp->red = pixels_tmp->blue;
			pixels_tmp->blue = color_tmp;
#endif

			//change opacity
			pixels_tmp->opacity = TransparentOpacity;
			pixels_tmp += 1;

		}
		rows[i] = png_malloc(png_ptr, color_format_bpp * width);
		memcpy(rows[i], pixels, color_format_bpp * width);
		pixels += width;

	}

	png_set_rows(png_ptr, info_ptr, (png_bytepp) rows);

	//write the image
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	png_destroy_write_struct(&png_ptr, &info_ptr);

	//clean up memory
	for (int i = 0; i < height; ++i) {
		png_free(png_ptr, rows[i]);
	}
	png_free(png_ptr, rows);

//	if (png_palette) {
//		png_free(png_ptr, png_palette);
//	}

}

int main(int argc, char *argv[]) {

	Image::Image input_file;
	ofstream output_file;

	try {

		po::options_description desc("Allowed options");
		desc.add_options()("help", "produce help message")("input,i",
				po::value<string>(), "path to input file")("output,o",
				po::value<string>(), "path to output file");

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

		if (input_file.isValid() && output_file.good()) {
			encode(input_file, output_file);
		}
		output_file.close();

	} catch (exception& e) {
		cerr << "error: " << e.what() << "\n";
		return 1;
	} catch (...) {
		cerr << "Exception of unknown type!\n";
	}

	return 0;

}
