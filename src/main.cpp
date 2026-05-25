#include <cstdio>
#include <cstring>
#include <string>

#include "../include/bpt.hpp"
#include "../include/stl/vector.hpp"

// Fixed-length string key to ensure POD storage on disk
struct FixedString65 {
	char data_[65];

	FixedString65() { std::memset(data_, 0, sizeof(data_)); }

	explicit FixedString65(const char* s) {
		std::memset(data_, 0, sizeof(data_));
		std::strncpy(data_, s, sizeof(data_) - 1);
	}

	explicit FixedString65(const std::string& s) : FixedString65(s.c_str()) {}
};

inline bool operator==(const FixedString65& a, const FixedString65& b) {
	return std::strcmp(a.data_, b.data_) == 0;
}

inline bool operator!=(const FixedString65& a, const FixedString65& b) {
	return !(a == b);
}

inline bool operator<(const FixedString65& a, const FixedString65& b) {
	return std::strcmp(a.data_, b.data_) < 0;
}

inline bool operator>(const FixedString65& a, const FixedString65& b) {
	return std::strcmp(a.data_, b.data_) > 0;
}

inline bool operator<=(const FixedString65& a, const FixedString65& b) {
	return std::strcmp(a.data_, b.data_) <= 0;
}

inline bool operator>=(const FixedString65& a, const FixedString65& b) {
	return std::strcmp(a.data_, b.data_) >= 0;
}

// Fast buffered IO for token-heavy input.
struct FastIO {
	static constexpr size_t IOBUF_SIZE = 1 << 16;
	char inbuf[IOBUF_SIZE];
	char outbuf[IOBUF_SIZE];
	size_t inpos;
	size_t insize;
	size_t outpos;

	FastIO() : inpos(0), insize(0), outpos(0) {}
	~FastIO() { flush(); }

	inline char read_char() {
		if (inpos >= insize) {
			insize = std::fread(inbuf, 1, IOBUF_SIZE, stdin);
			inpos = 0;
			if (insize == 0) {
				return 0;
			}
		}
		return inbuf[inpos++];
	}

	inline bool read_token(char* out, size_t maxlen) {
		char c = 0;
		do {
			c = read_char();
			if (!c) {
				return false;
			}
		} while (c <= ' ');

		size_t len = 0;
		while (c > ' ') {
			if (len + 1 < maxlen) {
				out[len++] = c;
			}
			c = read_char();
			if (!c) {
				break;
			}
		}
		out[len] = '\0';
		return true;
	}

	inline bool read_int(int& out) {
		char c = 0;
		do {
			c = read_char();
			if (!c) {
				return false;
			}
		} while (c <= ' ');

		int sign = 1;
		if (c == '-') {
			sign = -1;
			c = read_char();
		}
		int val = 0;
		while (c > ' ') {
			val = val * 10 + (c - '0');
			c = read_char();
			if (!c) {
				break;
			}
		}
		out = val * sign;
		return true;
	}

	inline void write_char(char c) {
		if (outpos >= IOBUF_SIZE) {
			flush();
		}
		outbuf[outpos++] = c;
	}

	inline void write_string(const char* s) {
		while (*s) {
			write_char(*s++);
		}
	}

	inline void write_int(int x) {
		static const char DIGIT_PAIRS[] =
			"00010203040506070809"
			"10111213141516171819"
			"20212223242526272829"
			"30313233343536373839"
			"40414243444546474849"
			"50515253545556575859"
			"60616263646566676869"
			"70717273747576777879"
			"80818283848586878889"
			"90919293949596979899";

		char tmp[16];
		char* p = tmp + sizeof(tmp);
		unsigned int v = 0;
		if (x < 0) {
			v = static_cast<unsigned int>(-static_cast<long long>(x));
		}
		else {
			v = static_cast<unsigned int>(x);
		}

		while (v >= 100) {
			unsigned int q = v / 100;
			unsigned int r = v - q * 100;
			const char* pair = DIGIT_PAIRS + r * 2;
			*--p = pair[1];
			*--p = pair[0];
			v = q;
		}
		if (v < 10) {
			*--p = static_cast<char>('0' + v);
		}
		else {
			const char* pair = DIGIT_PAIRS + v * 2;
			*--p = pair[1];
			*--p = pair[0];
		}
		if (x < 0) {
			*--p = '-';
		}

		size_t len = static_cast<size_t>((tmp + sizeof(tmp)) - p);
		if (outpos + len > IOBUF_SIZE) {
			flush();
		}
		std::memcpy(outbuf + outpos, p, len);
		outpos += len;
	}

	inline void flush() {
		if (outpos > 0) {
			std::fwrite(outbuf, 1, outpos, stdout);
			outpos = 0;
		}
		std::fflush(stdout);
	}
};

int main() {
	FastIO io;
	sjtu::BPlusTree<FixedString65, int> bpt;
	int q = 0;
	if (!io.read_int(q)) {
		return 0;
	}
	while (q--) {
		char op[8];
		char key[70];
		int val = 0;
		io.read_token(op, sizeof(op));
		if (op[0] == 'i') {
			io.read_token(key, sizeof(key));
			io.read_int(val);
			bpt.insert(FixedString65(key), val);
		}
		else if (op[0] == 'f') {
			io.read_token(key, sizeof(key));
			sjtu::vector<int> vec;
			bpt.find_all(FixedString65(key), vec);
			if (vec.empty()) {
				io.write_string("null\n");
			}
			else {
				for (int v : vec) {
					io.write_int(v);
					io.write_char(' ');
				}
				io.write_char('\n');
			}
		}
		else if (op[0] == 'd') {
			io.read_token(key, sizeof(key));
			io.read_int(val);
			bpt.erase(FixedString65(key), val);
		}
	}
	io.flush();
	return 0;
}