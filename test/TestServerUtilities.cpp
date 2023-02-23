//
// Created by ssthapa on 2/3/23.
//
//
// Created by ssthapa on 1/10/23.
//

#include "test.h"

#include <ServerUtilities.h>

TEST(trim) {
	std::string input = "Original string ";
	auto output = trim(input);
	auto expected = "Original string";
	ENSURE_EQUAL(expected, output, "String trimming failed");

	input = "Original string";
	output = trim(input);
	ENSURE_EQUAL(input,  output, "String trimmed too much");

	input = "Original string   ";
	output = trim(input);
	expected = "Original string";
	ENSURE_EQUAL(expected, output, "String trimming failed");

	input = "  Original string";
	output = trim(input);
	expected = "Original string";
	ENSURE_EQUAL(expected,  output, "String trimmed whitespace at beginning");

	input = "  Original string  ";
	output = trim(input);
	ENSURE_EQUAL(expected,  output, "String trimming failed");
}

TEST(parseStringSuffix) {
	std::string input = "5Ki";
	auto output = parseStringSuffix(input);
	long expected = 5 * 1024;
	ENSURE_EQUAL(expected, output, "5Ki not parsed correctly");
	std::transform(input.begin(), input.end(),input.begin(), [](unsigned char c) {return std::tolower(c);});
	ENSURE_EQUAL(expected, output, "5ki not parsed correctly");
	std::transform(input.begin(), input.end(),input.begin(), [](unsigned char c) {return std::toupper(c);});
	ENSURE_EQUAL(expected, output, "5KI not parsed correctly");

	input = "8Mi";
	output = parseStringSuffix(input);
	expected = 8L * 1024 * 1024;
	ENSURE_EQUAL(expected, output, "8Mi not parsed correctly");
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {return std::tolower(c);});
	ENSURE_EQUAL(expected, output, "8mi not parsed correctly");
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {return std::toupper(c);});
	ENSURE_EQUAL(expected, output, "8MI not parsed correctly");

	input = "3Gi";
	output = parseStringSuffix(input);
	expected = 3L * 1024 * 1024 * 1024;
	ENSURE_EQUAL(expected, output, "3Gi not parsed correctly");
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {return std::tolower(c);});
	ENSURE_EQUAL(expected, output, "3gi not parsed correctly");
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {return std::toupper(c);});
	ENSURE_EQUAL(expected, output, "3GI not parsed correctly");

	input = "7Ti";
	output = parseStringSuffix(input);
	expected = 7L * 1024 * 1024 * 1024 * 1024;
	ENSURE_EQUAL(expected, output, "7Ti not parsed correctly");
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {return std::tolower(c);});
	ENSURE_EQUAL(expected, output, "7ti not parsed correctly");
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {return std::toupper(c);});
	ENSURE_EQUAL(expected, output, "7TI not parsed correctly");
}

TEST(generateSuffixedString) {
	std::string expected = "5Ki";
	long input = 5 * 1024;
	ENSURE_EQUAL(expected, generateSuffixedString(input), "5Ki not parsed correctly");

	expected = "8Mi";
	input = 8L * 1024 * 1024;
	ENSURE_EQUAL(expected, generateSuffixedString(input), "8Mi not parsed correctly");

	expected = "3Gi";
	input = 3L * 1024 * 1024 * 1024;
	ENSURE_EQUAL(expected, generateSuffixedString(input), "3Gi not parsed correctly");

	expected = "7Ti";
	input = 7L * 1024 * 1024 * 1024 * 1024;
	ENSURE_EQUAL(expected, generateSuffixedString(input), "7Ti not parsed correctly");

	expected = "5.5Ki";
	input = 5 * 1024 + 512;
	ENSURE_EQUAL(expected, generateSuffixedString(input), "5Ki not parsed correctly");

}

