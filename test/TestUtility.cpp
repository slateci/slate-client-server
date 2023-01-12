//
// Created by ssthapa on 1/10/23.
//

#include "test.h"

#include <Utilities.h>

TEST(StringReplace) {
	std::string input = "Original string, really Original";
	replaceString(input, "Original", "New");
	ENSURE_EQUAL(input, "New string, really New", "String replacement failed");

	input = "Original string, really Original";
	replaceString(input, "Original", "New", 1);
	ENSURE_EQUAL(input,  "New string, really Original", "String replacement failed");

}


TEST(validDNSToken) {
	ENSURE_EQUAL(validDnsToken("uchicago01"), true, "uchicago01 is a valid dns token");
	ENSURE_EQUAL(validDnsToken("uchicago$01"), false, "uchicago$01 is not a valid dns token");
	ENSURE_EQUAL(validDnsToken("umich-prod"), true, "umich-prod is a valid dns token");
	ENSURE_EQUAL(validDnsToken("umichProd"),  true, "umichProd is a valid dns token");
}

TEST(validTagGroupName) {
	ENSURE_EQUAL(validTagGroupName("uchicago01") , true, "uchicago01 is a valid group tag name");
	ENSURE_EQUAL(validTagGroupName("uchicago$01"),  false, "uchicago$01 is not a valid group tag name");
	ENSURE_EQUAL(validTagGroupName("umich-prod"), true, "umich-prod is a valid group tag name");
	ENSURE_EQUAL(validTagGroupName("umichProd"), false, "umichProd is a valid group tag name");
}