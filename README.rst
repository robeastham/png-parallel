Parallelized PNG encoder using OpenMP and libpng
================================================

This is a parallelized PNG encoder written in C++ using OpenMP and libpng. 

**This application has to be seen as a proof of concept as it was developed for a seminar paper during my study.**

Feel free to use, enhance or whatever.


Requirements
------------
- zlib-dev
- libpng-dev


Compilation
-----------
Run "make all"


Execution
---------
./pngencoder -i INPUT_FILE.ext -o OUTPUT_FILE.png --num_threads=NUMBER


Known issues
------------
- Returns with error "pngencoder: magick/cache.c:2053: GetAuthenticPixelsCache: Assertion 'id < (long) cache_info->number_threads' failed." if the number of threads is higher than the available amount of CPUs. Seems to be a bug in ImageMagick.
- It does not use any filtering for the rows so the deflate compression ratio is pretty bad. 