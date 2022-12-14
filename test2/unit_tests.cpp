#include "catch/catch.hpp"

#include <Utilities.h>

TEST_CASE("Test string replacement 1")
{
    std::string input = "Original string, really Original";
    replaceString(input, "Original", "New");
    CHECK(input == "New string, really New");
}

TEST_CASE("Test string replacement 2")
{
    std::string input = "Original string, really Original";
    replaceString(input, "Original", "New", 1);
    CHECK(input == "New string, really Original");
}

TEST_CASE("Validate DNS token 1")
{
    CHECK(validateDnsToken("uchicago01") == true);
    CHECK(validateDnsToken("uchicago$01") == false);
    CHECK(validateDnsToken("umich-prod") == true);
    CHECK(validateDnsToken("umichProd") == true);
}

TEST_CASE("Validate tag/group name 1")
{
    CHECK(validateTagGroupName("uchicago01") == true);
    CHECK(validateTagGroupName("uchicago$01") == false);
    CHECK(validateTagGroupName("umich-prod") == true);
    CHECK(validateTagGroupName("umichProd") == false);
}