
#include "base.h"
#include "ldisk.h"
#include "file_system.h"


int main() {

	Ldisk myDisk;
	FileSystem myFileSystem(myDisk);
	bool run_system = true;
	std::string command = "";

	while (run_system && !std::cin.eof()) {

		std::getline(std::cin, command);

		if (command == "exit")
			run_system = false;
		else if (command == "")
			std::cout << "\n";
		else
			myFileSystem.give_command(command);
	}

	return 0;
}