#pragma once

#include "base.h";
#include "ldisk.h"

struct FILE_TABLE {

	char r_w[64];              //buffer for file
	int index;                 //index for file descriptor
	int buffer_index;		   //index in buffer
	int buffer_block;          //block in memory
};

class FileSystem {

private:

	Ldisk ldisk;
	const int OFT_SIZE = 4;
	bool is_initialized;
	FILE_TABLE open_file_table[4];

	void init_directory();
	void init_fs();

	void remove_descriptor(int desc_index);

	void defrag_block(char * block);

	void create_directory_entry(std::string file_name, int descriptor_index);
	int find_directory_entry(std::string file_name);
	void print_directory();

	int get_desc_index(std::string file_name);

	void insert_into_buffer(std::string data, char * buffer, int position);

	int find_oft_entry();
	bool is_oft_entry(int index);

	int create(std::string file_name);
	int destroy(std::string file_name);

	int open(std::string file_name);
	int close(int index);
	void close_all();

	int read(int index, std::string &mem_area, int count);
	int write(int index, std::string data);

	int lseek(int index, int pos);

	std::vector<std::string> directory();

public:

	FileSystem(Ldisk disk);
	~FileSystem() { close_all(); }   //close all files while being destroyed

	void give_command(std::string command);
};


FileSystem::FileSystem(Ldisk ldisk) : ldisk(ldisk), is_initialized(false) { /* need to call init_fs before using */}

void FileSystem::init_fs() {

	init_directory();

	//init all other OFT entries
	for (int i = 1; i < OFT_SIZE; i++) {

		open_file_table[i].index = -1;
		open_file_table[i].buffer_index = 0;
		open_file_table[i].buffer_block = 0;

		//init buffers
		for (int j = 0; j < 64; j++)
			open_file_table[i].r_w[j] = 0;
	}
}

void FileSystem::init_directory() {

	//init directory
	open_file_table[0].index = ldisk.get_directory_index();
	open_file_table[0].buffer_index = 0;

	//read first block into buffer
	std::vector<int> directory_descriptor = ldisk.get_descriptor(open_file_table[0].index);
	ldisk.read_block(directory_descriptor[1], open_file_table[0].r_w);
	open_file_table[0].buffer_block = directory_descriptor[1];
}

void FileSystem::print_directory() {

	std::vector<std::string> file_names = directory();
	if (file_names.size() > 0) {

		for (auto file_name : file_names)
			std::cout << file_name << " ";
	}
	std::cout << std::endl;
}

std::vector<std::string> FileSystem::directory() {

	FILE_TABLE * directory = &open_file_table[0];
	std::vector<int> dir_descriptor = ldisk.get_descriptor(directory->index);
	std::vector<std::string> file_names;
	std::string file_name;

	//find open entry (start with current block)
	for (int dir_block = 1; dir_block < 4; dir_block++) {  //each block

		ldisk.read_block(dir_descriptor[dir_block], directory->r_w);
		directory->buffer_block = dir_descriptor[dir_block];
		for (int j = 0; j < 64; j++) {  //each index in block

			if (directory->r_w[j] != NULL) {  //ignore empty spaces

				if (isInteger(std::string(1, directory->r_w[j]))) {

					file_names.push_back(file_name);
					file_name = "";
				}
				else
					file_name += directory->r_w[j];
			}
		}
	}

	return file_names;
}

int FileSystem::lseek(int index, int pos) {

	FILE_TABLE * curr_file = nullptr;
	std::vector<int> file_desc;
	int block_index = 0;
	int old_block = 0;

	if (is_oft_entry(index)) {

		curr_file = &open_file_table[index];
		file_desc = ldisk.get_descriptor(curr_file->index);
		old_block = curr_file->buffer_block;
		
		if (pos < file_desc[0]) {  //can't seek beyond EOF

			if (pos < 64)
				block_index = 1;
			else if ((pos >= 64) && (pos < 128))
				block_index = 2;
			else if ((pos >= 128) && (pos < 192))
				block_index = 3;

			if (file_desc[block_index] != 0) {  //check if in files range

				curr_file->buffer_index = (pos) - ((block_index - 1) * 64); //adjust index
				curr_file->buffer_block = file_desc[block_index];         //store current block

				if (curr_file->buffer_block != old_block)
					ldisk.read_block(curr_file->buffer_block, curr_file->r_w);  //read new block in if need be
			}
		}
		else
			return -1;
	}

	return curr_file->buffer_index;
}


int FileSystem::create(std::string file_name) {

	bool was_created = false;

	if ( (file_name.length() <= 4) && (find_directory_entry(file_name) == -1) ) {

		int file_descriptor = ldisk.init_descriptor(ldisk.find_free_block());  //create descriptor

		if (file_descriptor != -1) //if created create directory entry
			create_directory_entry(file_name, file_descriptor);
		else
			return -1;
	}
	else
		return -1;

	return 0;
}


int FileSystem::destroy(std::string file_name) {

	FILE_TABLE * directory = &open_file_table[0];
	int dir_location = find_directory_entry(file_name);
	bool clear_bytes = true;

	int original_location = dir_location;

	if (dir_location != -1) {

		//close the file first
		int desc_index = get_desc_index(file_name);
		for (int oft_index = 1; oft_index < 4; oft_index++) {
			if (open_file_table[oft_index].index == desc_index) {
				close(oft_index);
				break;
			}
		}

		while (clear_bytes) {

			if (isInteger(std::string(1, directory->r_w[dir_location]))) {

				remove_descriptor(directory->r_w[dir_location] - '0');   //convert arg to int and free descriptor
				clear_bytes = false;
			}
			directory->r_w[dir_location] = 0;
			dir_location++;
		}

		defrag_block(directory->r_w);
		ldisk.write_block(directory->buffer_block, directory->r_w);  //update directory on disk
	}
	else
		return -1;
}

void FileSystem::defrag_block(char * block) {

	char new_block[64];
	//initialize
	for (int i = 0; i < 64; i++)
		new_block[i] = 0;

	//defrag
	for (int i = 0; i < 64; i++)
		if (block[i] != NULL)
			new_block[i] = block[i];

	//copy to original block
	for (int i = 0; i < 64; i++)
		block[i] = new_block[i];
}

void FileSystem::remove_descriptor(int desc_index) {

	int block_counter = 0;
	std::vector<int> file_descriptor = ldisk.get_descriptor(desc_index);

	ldisk.destroy_descriptor(desc_index);
	for (auto desc_int : file_descriptor) { //release reserved blocks

		if (block_counter > 0)             //ignore file size
			ldisk.release_block(desc_int);
		block_counter++;
	}
}

int FileSystem::get_desc_index(std::string file_name) {

	int desc_index = -1;
	int dir_location = find_directory_entry(file_name);
	std::stringstream ss;

	if (dir_location != -1) {

		if (isInteger(std::string(std::string(1, open_file_table[0].r_w[dir_location + file_name.length() + 1])))) //check if double digit descriptor
			ss << open_file_table[0].r_w[dir_location + file_name.length() + 1];

		ss << open_file_table[0].r_w[dir_location + file_name.length()];
		ss >> desc_index;
	}

	return desc_index;
}

void FileSystem::create_directory_entry(std::string file_name, int descriptor_index) {

	FILE_TABLE * directory = &open_file_table[0];
	std::vector<int> dir_descriptor = ldisk.get_descriptor(directory->index);

	//create the entry
	std::stringstream ss;
	ss << file_name;
	ss << descriptor_index;

	//find open entry (start with current block)
	for (int dir_block = 1; dir_block < 4; dir_block++) {  //each block

		ldisk.read_block(dir_descriptor[dir_block], directory->r_w);
		directory->buffer_block = dir_descriptor[dir_block];
		for (int j = 0; j < 64; j++) {  //each index in block

			if (directory->r_w[j] == NULL) { //check if open entry

				insert_into_buffer(ss.str(), directory->r_w, j);
				ldisk.write_block(dir_descriptor[dir_block], directory->r_w);
				return;
			}
		}
	}
}

int FileSystem::find_directory_entry(std::string file_name) {

	std::string search_string = "";

	FILE_TABLE * directory = &open_file_table[0];
	std::vector<int> dir_descriptor = ldisk.get_descriptor(directory->index);

	//find open entry (start with current block)
	for (int dir_block = 1; dir_block < 4; dir_block++) {  //each block

		ldisk.read_block(dir_descriptor[dir_block], directory->r_w);
		directory->buffer_block = dir_descriptor[dir_block];
		for (int j = 0; j < 64; j++) {  //each index in block

			if (directory->r_w[j] != NULL) {  //ignore spaces

				if (isInteger(std::string(1, directory->r_w[j]))) {  //check if descriptor

					if (search_string == file_name)
						return j - search_string.length();  //start of this files entry
					else
						search_string = "";  //reset for next file_name
				}
				else
					search_string += directory->r_w[j];   //build current file_name
			}
		}
	}
	return -1;
}

void FileSystem::insert_into_buffer(std::string data, char * buffer, int position) {

	for (int i = 0; i < data.length(); i++, position++)
		buffer[position] = data[i];
}

bool FileSystem::is_oft_entry(int index) { 
	
	if ((index >= 0) && (index <= 3)) 
		return open_file_table[index].index != -1;
	else
		return false;
}

int FileSystem::open(std::string file_name) {

	FILE_TABLE * new_entry = nullptr;
	int oft_index = find_oft_entry();
	int desc_index = get_desc_index(file_name);
	std::vector<int> file_desc = ldisk.get_descriptor(desc_index);

	if ((oft_index != -1) && (desc_index != -1)) {

		//check if already open
		for (int i = 1; i < 4; i++) {
			if (open_file_table[i].index == desc_index)
				return -1;
		}

		//init the oft with the new file, read first block into memory
		new_entry = &open_file_table[oft_index];
		new_entry->index = desc_index;
		new_entry->buffer_block = file_desc[1];  //set to first block
		ldisk.read_block(new_entry->buffer_block, new_entry->r_w);  //read first block into memory
		new_entry->buffer_index = 0;

		return oft_index;
	}
	else
		return -1;

}

int FileSystem::close(int index) {

	FILE_TABLE * close_file = nullptr;

	if (is_oft_entry(index)) {

		close_file = &open_file_table[index];
		
		//write out block to be safe
		ldisk.write_block(close_file->buffer_block, close_file->r_w);

		//reset
		close_file->index = -1;
		close_file->buffer_index = 0;
		close_file->buffer_block = 0;
		return 0;
	}
	else
		return -1;
}

void FileSystem::close_all() {

	for (int i = 0; i < 4; i++) {

		if (is_oft_entry(i))
			close(i);
	}
}

int FileSystem::find_oft_entry() {

	for (int i = 0; i < OFT_SIZE; i++) {

		if (open_file_table[i].index == -1)
			return i;
	}
	return -1;
}

int FileSystem::write(int index, std::string data) {
	
	FILE_TABLE * curr_file = nullptr;
	std::vector<int> file_desc;
	int bytes_written = 0;
	int block_index = 0;

	if (is_oft_entry(index)) {

		curr_file = &open_file_table[index];
		file_desc = ldisk.get_descriptor(curr_file->index);

		//get index of block in file descriptor
		for (int i = 1; i < 4; i++)
			if (file_desc[i] == curr_file->buffer_block)
				block_index = i;

		for (int i = 0; i < data.length(); i++) {

			for (int j = curr_file->buffer_index; (j < 64) && (i < data.length()); j++, i++, curr_file->buffer_index++, bytes_written++) { //write bytes
				curr_file->r_w[j] = data[i];
			}

			ldisk.write_block(curr_file->buffer_block, curr_file->r_w);   //write out data
			block_index++;

			if ((i < data.length()) && (block_index < 4)) {

				if (file_desc[block_index] == 0) {  //check if there isnt another block to read

					int new_block = ldisk.find_free_block();
					ldisk.update_descriptor_blocks(curr_file->index, new_block);  //update descriptor on disk
					file_desc[block_index] = new_block;
				}

				ldisk.read_block(file_desc[block_index], curr_file->r_w);  //read in block
				curr_file->buffer_block = file_desc[block_index];
				curr_file->buffer_index = 0;                               //start from beginning of next block
				i--;     //go back and write char you missed when loading new block
			}
			else
				break;   //over 3 blocks, just exit
		}

		//update size in cache
		if (file_desc[0] == 1)
			ldisk.update_descriptor_size(curr_file->index, bytes_written); 
		else
			ldisk.update_descriptor_size(curr_file->index, file_desc[0] + bytes_written);

		return bytes_written;
	}
	else
		return -1;
}


int FileSystem::read(int index, std::string &mem_area, int count) {

	FILE_TABLE * curr_file = nullptr;
	std::vector<int> file_desc;
	int block_index = 0;
	int bytes_read = 0;

	if (is_oft_entry(index)) {

		curr_file = &open_file_table[index];
		file_desc = ldisk.get_descriptor(curr_file->index);

		//get index of block in file descriptor
		for (int i = 1; i < 4; i++)
			if (file_desc[i] == curr_file->buffer_block)
				block_index = i;

		for (int i = 0; i < count; i++) {

			int j = 0;
			for (j = curr_file->buffer_index; (j < 64) && (i < count); j++, i++, curr_file->buffer_index++, bytes_read++) { //read bytes
				if ( curr_file->r_w[j] != 0 )
					mem_area += curr_file->r_w[j];
			}


			if (j >= 64) {
				block_index++;   // go to next block
			}
			
			if ((i < count) && (block_index < 4)) {

				if (file_desc[block_index] == 0) { //cant read what isnt there
					break;
				}
				else {
					ldisk.read_block(file_desc[block_index], curr_file->r_w);
					curr_file->buffer_block = file_desc[block_index];
					curr_file->buffer_index = 0;
				}
				i--; //go back and read char you missed when you needed to load next block
			}
			else {
				break;
			}
		}

		return bytes_read;
	}
	else
		return -1;
}

void FileSystem::give_command(std::string command) {

	std::stringstream ss(command);
	std::string token;
	std::vector<std::string> command_tokens;

	//tokenize the command 
	if (command != "") {

		int counter = 0;
		while (std::getline(ss, token, ' '))
			command_tokens.push_back(token);
	}
	else
		command_tokens.push_back("NO INPUT");

	//make sure they called init
	if ((command_tokens[0] != "in") && !is_initialized) {

		std::cout << "error" << std::endl;
		return;
	}

	//run command
	if (command_tokens[0] == "cr") {

		if (create(command_tokens[1]) != -1)
			std::cout << command_tokens[1] << " created" << std::endl;
		else
			std::cout << "error" << std::endl;

	}
	else if (command_tokens[0] == "de") {

		if (destroy(command_tokens[1]) != -1)
			std::cout << command_tokens[1] << " destroyed " << std::endl;
		else
			std::cout << "error" << std::endl;
	}
	else if (command_tokens[0] == "op") {

		int oft_index = open(command_tokens[1]);
		if (oft_index != -1)
			std::cout << command_tokens[1] << " opened " << oft_index << std::endl;
		else
			std::cout << "error" << std::endl;
	}
	else if (command_tokens[0] == "cl") {

		if ((isInteger(command_tokens[1])) && close(std::stoi(command_tokens[1])) != -1) {

			std::cout << command_tokens[1] << " closed" << std::endl;
		}
		else
			std::cout << "error" << std::endl;

	}
	else if (command_tokens[0] == "wr") {

		std::string data = "";
		int oft_index = -1;

		if ( isInteger(command_tokens[1]) )
			oft_index = std::stoi(command_tokens[1]);

		if (is_oft_entry(oft_index)) {

			data.append(std::stoi(command_tokens[3]), command_tokens[2][0]);      //create data string
			std::cout << write(oft_index, data) << " bytes written" << std::endl; //write to file
		}
		else
			std::cout << "error" << std::endl;
	}
	else if (command_tokens[0] == "rd") {

		int oft_index = std::stoi(command_tokens[1]);
		int read_count = std::stoi(command_tokens[2]);
		std::string out_data;

		if (is_oft_entry(oft_index)) {

			read(oft_index, out_data, read_count);
			std::cout << out_data << std::endl;
		}
		else
			std::cout << "error" << std::endl;
	}
	else if (command_tokens[0] == "sk") {

		int oft_index = std::stoi(command_tokens[1]);
		if (is_oft_entry(oft_index)) {
			
			int seek_result = lseek(oft_index, std::stoi(command_tokens[2]));
			if (seek_result != -1)
				std::cout << "position is " << std::stoi(command_tokens[2]) << std::endl;  //just re-printing what they put in
			else
				std::cout << "error" << std::endl;
		}
		else
			std::cout << "error" << std::endl;
	}
	else if (command_tokens[0] == "dr") {

		print_directory();
	}
	else if (command_tokens[0] == "in") {

		is_initialized = true;

		if (command_tokens.size() > 1)
			ldisk.init_disk(command_tokens[1]);
		else
			ldisk.init_disk();

		init_fs();
	}
	else if (command_tokens[0] == "sv") {

		close_all();
		ldisk.save_disk(command_tokens[1]);
		std::cout << "disk saved" << std::endl;
	}
	else if (command_tokens[0] == "dump") {

		ldisk.dump_disk();
	}
	else if (command_tokens[0] == "desc") {

		std::cout << "FILE DESCRIPTORS " << std::endl;
		for (int i = 0; i < 24; i++) {  //print all descriptors

			std::cout << "DESC " << i << ": ";
			std::vector<int> file_desc = ldisk.get_descriptor(i);
			for (auto desc_int : file_desc)
				std::cout << desc_int << " ";
			std::cout << std::endl;
		}
	}
	else if (command_tokens[0] == "oft") {

		std::cout << "OPEN FILE TABLE" << std::endl;
		for (int i = 0; i < 4; i++) {

			if (is_oft_entry(i)) {

				std::cout << "DESC INDEX: " << open_file_table[i].index << std::endl;
				std::cout << "BUFFER INDEX: " << open_file_table[i].buffer_index << std::endl;
			}
		}
	}
	else
		std::cout << "error" << std::endl;
}
