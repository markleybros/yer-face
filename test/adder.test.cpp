#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "adder.hpp"

namespace {

TEST(Adder, ReturnsFourWhenPassedTwoAndTwo) {
	ASSERT_EQ(adder(2, 2), 4);
}

TEST(Adder, ReturnsTwoWhenPassedTwoAndZero) {
	ASSERT_EQ(adder(2, 0), 2);
}

TEST(Adder, ReturnsMinusTwoWhenPassedNegativeOneAndNegativeOne) {
	ASSERT_EQ(adder(-1, -1), -2);
}

} //namespace
