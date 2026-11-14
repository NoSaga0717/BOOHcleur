#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/InputRingBuffer.h"
#include "boohcleur/core/SpscQueue.h"

#include <vector>

using namespace boohcleur;

TEST_CASE("Tampon circulaire : relecture et disponibilite")
{
	InputRingBuffer ring;
	ring.prepare(2, 100);

	std::vector<float> l(30), r(30);

	for (int block = 0; block < 5; ++block)
	{
		for (int i = 0; i < 30; ++i)
		{
			l[i] = static_cast<float>(block * 30 + i);
			r[i] = -l[i];
		}

		const float* in[2] = { l.data(), r.data() };
		ring.write(in, 2, 30);
	}

	CHECK(ring.writePosition() == 150);
	CHECK(ring.oldestAvailable() == 50);
	CHECK_FALSE(ring.isAvailable(49, 1));
	CHECK(ring.isAvailable(50, 100));
	CHECK_FALSE(ring.isAvailable(140, 11));

	std::vector<float> a(20), b(20);
	float* out[2] = { a.data(), b.data() };
	REQUIRE(ring.read(95, 20, out));
	CHECK(a[0] == 95.0f);
	CHECK(a[19] == 114.0f);
	CHECK(b[5] == -100.0f);
	CHECK_FALSE(ring.read(10, 5, out));
}

TEST_CASE("Tampon circulaire : entree mono dupliquee")
{
	InputRingBuffer ring;
	ring.prepare(2, 16);
	std::vector<float> mono { 1, 2, 3, 4 };
	const float* in[1] = { mono.data() };
	ring.write(in, 1, 4);
	CHECK(ring.sample(0, 2) == 3.0f);
	CHECK(ring.sample(1, 2) == 3.0f);
}

TEST_CASE("File SPSC : ordre et capacite")
{
	SpscQueue<int, 4> queue;
	CHECK(queue.push(1));
	CHECK(queue.push(2));
	CHECK(queue.push(3));
	CHECK_FALSE(queue.push(4));	// capacité utile : 3
	CHECK(queue.pop() == 1);
	CHECK(queue.push(4));
	CHECK(queue.pop() == 2);
	CHECK(queue.pop() == 3);
	CHECK(queue.pop() == 4);
	CHECK_FALSE(queue.pop().has_value());
}
