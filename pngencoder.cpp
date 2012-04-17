//============================================================================
// Name		: pngencoder.cpp
// Author	  : Pascal Beyeler
// Version	 :
// Copyright   : Copyright ist Aberglaube
// Description : PNG encoder with OpenMP support
//============================================================================


#include <boost/program_options.hpp>
#include <iostream>
#include <iterator>
#include <fstream>
#include <Magick++/Include.h>
#include <Magick++/Image.h>
#include <Magick++/Pixels.h>

using namespace std;
using namespace Magick;
namespace po = boost::program_options;

void encode(Image::Image &input_file, ofstream &output_file) {

	//access pixel data
	input_file.modifyImage();
	ColorRGB rgb(input_file.pixelColor(0,0));
	Pixels pixel_cache(input_file);
	PixelPacket* pixels;
	pixels = pixel_cache.get(0, 0, input_file.columns(), input_file.rows());
	cout << rgb.red() * 255 << "  " << pixel_cache.columns() << "  " << pixel_cache.rows() << "  " << (255.0 / MaxRGB * pixels->blue) << " " << MaxRGB << endl;


}

int main(int argc, char *argv[]) {

	Image::Image input_file;
	ofstream output_file;

	try {

		po::options_description desc("Allowed options");
		desc.add_options()
			("help", "produce help message")
			("input,i", po::value<string>(), "path to input file")
			("output,o", po::value<string>(), "path to output file")
		;

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
