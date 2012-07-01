//============================================================================
// Name			: PNGParallel.cpp
// Author		: Pascal Beyeler
// Version		: 1.0
// Copyright	: This content is released under the http://www.opensource.org/licenses/mit-license.php MIT License.
// Description	: Parallelized PNG encoder with OpenMP
//============================================================================


#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <Magick++/Image.h>
#include "PNGParallel.h"

using namespace std;
using namespace Magick;
namespace po = boost::program_options;

int main(int argc, char *argv[]) {

	Image::Image inputFile;
	ofstream outputFile;
	int numThreads = 2;

	try {

		//Command line argument handling
		po::options_description desc("Allowed options");
		desc.add_options()
			("help", "produce help message")
			("input,i", po::value<string>(), "path to input file")
			("output,o", po::value<string>(), "path to output file")
			("num_threads,nt", po::value<int>()->default_value(numThreads), "number of threads used for the compression");

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("help")) {
			cout << desc << "\n";
			return 1;
		}

		if (vm.count("input")) {
			inputFile.read(vm["input"].as<string>());
		} else {
			cout << "Input file was not set.\n";
			return 1;
		}

		if (vm.count("output")) {
			outputFile.open(vm["output"].as<string>().c_str());
			if (!outputFile.good()) {
				cout << "Output file could not be opened" << endl;
				return 1;
			}
		} else {
			cout << "Output file was not set.\n";
			return 1;
		}

		if (vm.count("num_threads")) {
			numThreads = vm["num_threads"].as<int>();
		}

		//Encode the file if everything is okay
		if (inputFile.isValid() && outputFile.good()) {

			PNGParallel pngParallel(inputFile);
			pngParallel.setCompressionLevel(9);
			pngParallel.setNumThreads(numThreads);
			pngParallel.compress(outputFile);

		}
		outputFile.close();

	} catch (exception& e) {
		cerr << "error: " << e.what() << "\n";
		return 1;
	} catch (...) {
		cerr << "Exception of unknown type!\n";
	}

	return 0;

}
