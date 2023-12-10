#pragma once
#include <iostream>

#include <string>
#include <curl/curl.h>
#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>
#include <json/value.h>

class Parser {
public:
	Parser(const std::string& s) : url(s) {}
	Parser() {}
	void request();
	std::string get_json_string();
	Json::Value get_json() { return root; }
private:
	std::string url;
	Json::Value root;

	static std::size_t write_callback(char* in, std::size_t size, std::size_t nmemb, std::string* out);
};