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
