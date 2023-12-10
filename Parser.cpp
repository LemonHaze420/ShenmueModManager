#include "Parser.h"

std::string Parser::get_json_string() {
	Json::FastWriter fastWriter;
	return fastWriter.write(root);
}

std::size_t Parser::write_callback(char* in, size_t size, size_t nmemb, std::string* out) {
	std::size_t total_size = size * nmemb;
	if (total_size) {
		out->append(in, total_size);
		return total_size;
	}
	return 0;
}

void Parser::request() {
	// Parse raw Json string
	std::string str_buffer;
	CURL* curl = curl_easy_init();
	if (curl) {
		CURLcode res;
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str_buffer);
		res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
#			ifdef DEBUG_CONSOLE
				std::cerr << "curl_easy_perform() failed:" << curl_easy_strerror(res) << std::endl;
#			endif
			return;

		}
		curl_easy_cleanup(curl);
	}
	else {
#	ifdef DEBUG_CONSOLE
		std::cout << "Could not initialize curl" << std::endl;
#	endif
	}

	Json::Reader reader;
	bool parse_status = reader.parse(str_buffer.c_str(), root);
	if (!parse_status) {
#	ifdef DEBUG_CONSOLE
		std::cerr << "parse() failed: " << reader.getFormattedErrorMessages() << std::endl;
	#endif
		return;
	}
	else {
#		ifdef DEBUG_CONSOLE
			std::cout << "parser returned " << reader.good() << std::endl;

			Json::StreamWriterBuilder wbuilder;
			std::string outputConfig = Json::writeString(wbuilder, root);
			std::cout << outputConfig << std::endl;
#		endif
	}
}