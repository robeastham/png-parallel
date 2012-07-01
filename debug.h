#include <iostream>

using namespace std;

void dump(char* address, int size) {
	char *start = address;
	for(int i = 0; i < size; i++) {
		cout << (int)*start << " ";
		start++;
	}
	cout << endl;
}
