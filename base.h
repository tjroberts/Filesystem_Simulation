#pragma once


#include<bitset>
#include<iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>


//FROM http://stackoverflow.com/questions/2844817/how-do-i-check-if-a-c-string-is-an-int
inline bool isInteger(const std::string & s) {

	if (s.empty() || ((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+'))) return false;

	char * p;
	strtol(s.c_str(), &p, 10);

	return (*p == 0);
}