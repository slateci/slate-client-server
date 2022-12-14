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
    CHECK(validDnsToken("uchicago01") == true);
    CHECK(validDnsToken("uchicago$01") == false);
    CHECK(validDnsToken("umich-prod") == true);
    CHECK(validDnsToken("umichProd") == true);
}

TEST_CASE("Validate tag/group name 1")
{
    CHECK(validTagGroupName("uchicago01") == true);
    CHECK(validTagGroupName("uchicago$01") == false);
    CHECK(validTagGroupName("umich-prod") == true);
    CHECK(validTagGroupName("umichProd") == false);
}