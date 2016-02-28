#pragma once

#include "base.h"

//Logical disk for the filesystem

/*
LDISK INFO - 

[0] - This is the bitmap, shows what blocks are open for use

(EACH INDEX 16 bits)
[1 - 6] - File descriptors, each can contain 3 integers that specify blocks the file uses and one integer for file size

(EACH INDEX 8 bits)
[7 - 9] - These are the blocks for the directory file, contains file name and index of descriptor
*/

//FROM http://stackoverflow.com/questions/21128331/how-do-you-efficiently-support-sub-bitstrings-in-a-bitset-like-class-in-c11
template<size_t bits>
std::bitset<bits> subset(std::bitset<bits> set, int min, int max) {

	const int ignore_hi = bits - max;
	std::bitset<bits> range = (~std::bitset<bits>() << ignore_hi) >> ignore_hi;
	set &= range;
	return set >> min;
}

class Ldisk {

private:

	//ldisk defined sizes
	static const int BLOCK_SIZE = 512;  //bits
	static const int NUM_BLOCKS = 64;   //bits
	static const int BYTE_SIZE = 8;     //bits
	static const int CACHE_SIZE = 7;   //size of the cache

	static const int DESC_INDEX = 16;   //bits (index in descriptor portion)
	static const int DIR_INDEX = 8;     //bits (index in directory portion)
	
	static const int FILE_BLOCK_START = 7;

	//descriptor bounds
	static const int DESCRIPTOR_START = 1;
	static const int DESCRIPTOR_END = 6;

	static const int INT_SIZE = 32;  //bits
	static const int CHAR_SIZE = 8;  //bits

	std::bitset<BLOCK_SIZE> ldisk[NUM_BLOCKS];  //logical disk
	std::bitset<BLOCK_SIZE> cache[CACHE_SIZE];  //cahce for bitmap/file descriptors

	int directory_descriptor;

	void clear_disk();

	void write_cache();
	void read_cache();

	int read_int(std::bitset<BLOCK_SIZE> block, int start);      //read int at index
	char read_char(std::bitset<BLOCK_SIZE> block, int start);    //read char at index

	void write_int(std::bitset<BLOCK_SIZE> * block, int start, int insert_int);

	std::pair<int, int> get_desc_location(int desc_index);       //returns block index, and bit index

public:

	Ldisk();

	void dump_disk();   //DEBUG!!!!!!!!!!!!!!!!!

	void read_block(int i, char * p);
	void write_block(int i, char * p);

	int find_free_block();
	inline void release_block(int block_num) { cache[0][block_num] = 0; }

	void save_disk(std::string file_name);
	void init_disk(std::string file_name);
	void init_disk();

	int init_descriptor(int new_block);                         //create new file descriptor, return index
	void destroy_descriptor(int desc_index);					//destroy file descriptor
	void update_descriptor_blocks(int desc_index, int new_block);      //add a block to existing descriptor
	void update_descriptor_size(int desc_index, int new_size);         //change file size in descriptor
	std::vector<int> get_descriptor(int desc_index);
	
	inline int get_directory_index() { return directory_descriptor; }
};

Ldisk::Ldisk() {   /*need to call init to use this object */   }

std::vector<int> Ldisk::get_descriptor(int desc_index) {

	std::vector<int> file_blocks;
	std::pair<int, int> desc_location = get_desc_location(desc_index);
	int desc_integer = 0;

	for (int i = desc_location.second; i < (desc_location.second + (4 * INT_SIZE)); i += INT_SIZE) {

		desc_integer = read_int(cache[desc_location.first], i);
		file_blocks.push_back(desc_integer);
	}

	return file_blocks;
}

std::pair<int, int> Ldisk::get_desc_location(int desc_index) {

	std::pair<int, int> desc_location;

	//find the block its in
	if (desc_index < 4)
		desc_location.first = DESCRIPTOR_START;
	else if (desc_index < 8)
		desc_location.first = DESCRIPTOR_START + 1;
	else if (desc_index < 12)
		desc_location.first = DESCRIPTOR_START + 2;
	else if (desc_index < 16)
		desc_location.first = DESCRIPTOR_START + 3;
	else if (desc_index < 20)
		desc_location.first = DESCRIPTOR_START + 4;
	else
		desc_location.first = DESCRIPTOR_START + 5;

	//find the index in the block

	desc_location.second = (desc_index - ((desc_location.first - 1) * 4)) * (INT_SIZE * 4);

	return desc_location;
}

int Ldisk::read_int(std::bitset<BLOCK_SIZE> block, int start) {

	int end = start + (INT_SIZE - 1);
	//convert to integer
	std::bitset<BLOCK_SIZE> integer_bits = subset(block, start, end);

	return integer_bits.to_ulong();
}

void Ldisk::write_int(std::bitset<BLOCK_SIZE> * block, int start, int insert_int) {

	std::bitset<INT_SIZE> integer_binary(insert_int);

	int j = start;
	for (int i = 0; i < INT_SIZE; i++, j++)
		(*block)[j] = integer_binary[i];
}

char Ldisk::read_char(std::bitset<BLOCK_SIZE> block, int start) {

	int end = start + (BYTE_SIZE - 1);
	//convert
	std::bitset<BLOCK_SIZE> char_bits = subset(block, start, end);

	return char(char_bits.to_ulong());
}

int Ldisk::init_descriptor(int new_block) {  

	int desc_index = 0;

	for (int i = DESCRIPTOR_START; i <= DESCRIPTOR_END; i++) {

		for (int j = 0; j < BLOCK_SIZE; j+=(4*INT_SIZE), desc_index++) {

			int curr_block = read_int(cache[i], j);  //check integer at index
			if (curr_block == 0) {

				//create new entry
				write_int(&cache[i], j, 1);
				write_int(&cache[i], j + INT_SIZE, new_block);
				return desc_index;
			}
		}
	}
	return -1;
}

void Ldisk::destroy_descriptor(int desc_index) {

	std::pair<int, int> desc_location = get_desc_location(desc_index); //get its location
	int desc_end = desc_location.second + (4 * INT_SIZE);
	int block_counter = 0;

	for (int i = desc_location.second; i < desc_end; i += INT_SIZE, block_counter++) {  //delete four integers from descriptor

		write_int(&cache[desc_location.first], i, 0);
	}
}

void Ldisk::update_descriptor_blocks(int desc_index, int new_block) {

	//find descriptor location
	std::pair<int,int> desc_location = get_desc_location(desc_index);

	//skip over file size
	desc_location.second += INT_SIZE;

	//insert new block
	int desc_count = 0;
	for (desc_count = 1; desc_count < 4; desc_count++, desc_location.second += INT_SIZE) {

		if (read_int(cache[desc_location.first], desc_location.second) == 0) {

			write_int(&cache[desc_location.first], desc_location.second, new_block);
			break;
		}
	}
}

void Ldisk::update_descriptor_size(int desc_index, int new_size) {

	//find descriptor location
	std::pair<int, int> desc_location = get_desc_location(desc_index);
	write_int(&cache[desc_location.first], desc_location.second, new_size);
}

void Ldisk::read_cache() {

	//using fast method
	for (int i = 0; i < CACHE_SIZE; i++) {

		for (int j = 0; j < BLOCK_SIZE; j++) 
			cache[i][j] = ldisk[i][j];
	}

	/* using read_block() method

	std::string bit_string = "";
	
	char in[64];
	for (int i = 0; i < 64; i++)
		in[i] = 0;

	for (int i = 0; i < CACHE_SIZE; i++) {

		//read from disk
		read_block(i, in);
		
		//convert to bit string
		for (int j = 0; j < BLOCK_SIZE; j++)
			bit_string += std::bitset<CHAR_SIZE>(in[j]).to_string();

		//store bits in cache
		for (int j = 0; j < BLOCK_SIZE; j++)
			cache[i][j] = std::stoi(std::string(1,bit_string[j]));

		bit_string = "";
	}
	*/
}

void Ldisk::write_cache() {
	
	// using fast method
	for (int i = 0; i < CACHE_SIZE; i++) {

		for (int j = 0; j < BLOCK_SIZE; j++)
			ldisk[i][j] = cache[i][j];
	}
	

	/* using write_block() method

	std::string bit_string = "";

	char out[64];
	for (int i = 0; i < 64; i++)
		out[i] = 0;

	for (int i = 0; i < CACHE_SIZE; i++) {

		//bit_string = cache[i].to_string();
		int char_counter = 0;
		for (int j = 0; j < BLOCK_SIZE; j += CHAR_SIZE, char_counter++) {
			std::bitset<BLOCK_SIZE> sub_bits = subset(cache[i], j, j + CHAR_SIZE);
			out[char_counter] = char(sub_bits.to_ulong());
		}
		write_block(i, out);
	}
	*/
}

void Ldisk::clear_disk() {

	for (int i = 0; i < NUM_BLOCKS; i++)
		ldisk[i].reset();
}

int Ldisk::find_free_block() {

	for (int i = FILE_BLOCK_START; i < NUM_BLOCKS; i++) {

		if (cache[0][i] == 0) {
			cache[0][i] = 1;
			return i;
		}
	}
}

//reads an entire block into the buffer
void Ldisk::read_block(int i, char * p) {

	std::string bit_string = ldisk[i].to_string();
	std::reverse(bit_string.begin(), bit_string.end());  //reverse because of bit ordering
	
	int char_counter = 0;
	std::stringstream sstream(bit_string);
	for (int i = 0; i < (BLOCK_SIZE/BYTE_SIZE); i++)
	{
		std::bitset<8> bits;
		sstream >> bits;
		*(p + char_counter) = char(bits.to_ulong());

		 char_counter++;
	}
}	

//writes a block from the buffer
void Ldisk::write_block(int i, char * p) {

	std::string bit_string = "";
	
	//convert to binary
	for (int j = 0; j < (BLOCK_SIZE/BYTE_SIZE); j++)
		bit_string += std::bitset<BYTE_SIZE>(p[j]).to_string();

	//store binary bit by bit
	for (int j = 0; j < BLOCK_SIZE; j++)
		ldisk[i][j] = bit_string[j] - '0';
	
}

void Ldisk::save_disk(std::string file_name) {

	std::ofstream outFile;
	outFile.open(file_name);
	std::string bit_string;
	write_cache();

	for (auto block : ldisk) {

		bit_string = block.to_string();
		std::reverse(bit_string.begin(), bit_string.end());  //reverse (maintain endianness)

		outFile << bit_string << std::endl;
	}
}

void Ldisk::init_disk(std::string file_name) {

	std::ifstream inFile(file_name);
	std::stringstream ss;
	std::string line;
	int block_counter = 0;

	if (inFile) {

		while (std::getline(inFile, line)) {
			
			for (int bit_counter = 0; bit_counter < BLOCK_SIZE; bit_counter++)
				ldisk[block_counter][bit_counter] = line[bit_counter] - '0';

			block_counter++;
		}

		read_cache();
		directory_descriptor = 0;  //always first descriptor
		std::cout << "disk restored" << std::endl;
	}
	else
		init_disk();
}

void Ldisk::init_disk() {

	clear_disk();
	read_cache();

	//set up directory descriptor (give three blocks)
	directory_descriptor = init_descriptor(find_free_block());

	update_descriptor_blocks(directory_descriptor, find_free_block());
	update_descriptor_blocks(directory_descriptor, find_free_block());

	std::cout << "disk initialized" << std::endl;
}


void Ldisk::dump_disk() {

	std::cout << "CACHE " << std::endl;
	for (auto cache_block : cache)
		std::cout << cache_block.to_string() << std::endl;

	std::cout << "DISK " << std::endl;
	for (auto block : ldisk)
		std::cout << block.to_string() << std::endl;
}