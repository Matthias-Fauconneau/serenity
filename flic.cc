#define Type typename
#define generic template<Type T>
template<Type A, Type B> constexpr bool operator !=(const A& a, const B& b) { return !(a==b); }

typedef char byte;
typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
typedef signed int int32;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef unsigned long ptr;
typedef signed long long int64;
typedef unsigned long long uint64;
typedef __SIZE_TYPE__ 	size_t;

/// Numeric range
struct range {
	range(int size) : start(0), stop(size) {}
	struct iterator {
		int i;
		int operator*() { return i; }
		iterator& operator++() { i++; return *this; }
		bool operator !=(const iterator& o) const { return i<o.i; }
	};
	iterator begin() const { return {start}; }
	iterator end() const { return {stop}; }
	int start, stop;
};

/// Unmanaged fixed-size const reference to an array of elements
generic struct ref {
	typedef T type;
	const T* data = 0;
	size_t size = 0;

	/// references \a size elements from const \a data pointer
	constexpr ref(const T* data, size_t size) : data(data), size(size) {}
	const T* begin() const { return data; }
	const T* end() const { return data+size; }
	const T& operator [](size_t i) const { return data[i]; }

	/// Slices a reference to elements from \a pos to \a pos + \a size
	ref<T> slice(size_t pos, size_t size) const { return ref<T>(data+pos, size); }
	/// Slices a reference to elements from \a pos to the end of the reference
	ref<T> slice(size_t pos) const { return ref<T>(data+pos,size-pos); }

	/// Compares all elements
	bool operator ==(const ref<T> o) const {
		if(size != o.size) return false;
		for(size_t i: range(size)) if(data[i]!=o.data[i]) return false;
		return true;
	}
};

#define assert(expr, message...) ({})

/// Unmanaged fixed-size mutable reference to an array of elements
generic struct mref : ref<T> {
	using ref<T>::data;
	using ref<T>::size;

	/// Default constructs an empty reference
	mref(){}
	/// references \a size elements from \a data pointer
	mref(T* data, size_t size) : ref<T>(data,size) {}

	T& operator [](size_t i) const { return (T&)((ref<T>&)(*this))[i]; }

	/// Slices a reference to elements from \a pos to \a pos + \a size
	mref<T> slice(size_t pos, size_t size) const { assert(pos+size <= this->size, pos, size, this->size); return mref<T>((T*)data+pos, size); }
	/// Slices a reference to elements from to the end of the reference
	mref<T> slice(size_t pos) const { assert(pos<=size); return mref<T>((T*)data+pos,size-pos); }
};

// C runtime memory allocation
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size) noexcept; // stdlib.h
extern "C" void free(void* buffer) noexcept; // malloc.h
/// Managed fixed capacity mutable reference to an array of elements
/// \note Data is either an heap allocation managed by this object or a reference to memory managed by another object.
generic struct buffer : mref<T> {
	using mref<T>::data;
	using mref<T>::size;
	size_t capacity = 0; /// 0: reference, >0: size of the owned heap allocation

	/// Allocates an uninitialized buffer for \a capacity elements
	buffer(size_t capacity, size_t size) : mref<T>((T*)0, size), capacity(capacity) {
		if(!capacity) return;
		posix_memalign((void**)&data, 64, capacity*sizeof(T));
	}
	explicit buffer(size_t size) : buffer(size, size) {}

	/// If the buffer owns the reference, returns the memory to the allocator
	~buffer() { if(capacity) free((void*)data); }
};

/// Encodes packed bitstreams (msb)
struct BitWriter {
	uint8* pointer = 0;
	uint64 word = 0;
	int64 bitLeftCount = 64;
	uint8* end = 0;
	BitWriter() {}
	BitWriter(mref<byte> buffer) : pointer((uint8*)buffer.begin()), end((uint8*)buffer.end()) {}
	void write(uint size, uint64 value) {
		if(size < bitLeftCount) {
			word <<= size;
			word |= value;
		} else {
			word <<= bitLeftCount;
			word |= value >> (size - bitLeftCount); // Puts leftmost bits in remaining space
			assert(pointer < end);
			*(uint64*)pointer = __builtin_bswap64(word); // MSB
			pointer += 8;
			bitLeftCount += 64;
			word = value; // Already stored leftmost bits will be pushed out eventually
		}
		bitLeftCount -= size;
	}
	void flush() {
		if(bitLeftCount<64) word <<= bitLeftCount;
		while(bitLeftCount<64) { assert(pointer < end); *pointer++ = word>>(64-8); word <<= 8; bitLeftCount += 8; }
	}
};

/// Decodes packed bitstreams (msb)
struct BitReader {
	uint8* pointer = 0;
	uint64 word = 0;
	int64 bitLeftCount = 64;
	uint8* begin = 0;
	uint8* end = 0;
	BitReader() {}
	BitReader(ref<byte> data) : pointer((uint8*)data.begin()), begin(pointer), end((uint8*)data.end()) {
		word = __builtin_bswap64(*(uint64*)pointer);
		pointer += 8;
	}
	uint read(uint size) {
		if(bitLeftCount < size/*~12*/) refill(); // conservative. TODO: fit to largest code to refill less often
		uint x = word >> (64-size);
		word <<= size;
		bitLeftCount -= size;
		return x;
	}
	void refill() {
		uint64 next = __builtin_bswap64(*(uint64*)pointer);
		int64 byteCount = (64-bitLeftCount)>>3;
		pointer += byteCount;
		word |= next >> bitLeftCount;
		bitLeftCount += byteCount<<3;
	}
	template<uint r, size_t z=14> uint readExpGolomb() {
		if(bitLeftCount <= int64(z)) refill();
		// 'word' is kept left aligned, bitLeftCount should be larger than maximum exp golomb zero run length
		size_t size = __builtin_clzl(word);
		uint b = size * 2 + 1 + r;
		return read(b) - (1<<r);
	}
};

/// Decodes packed bitstreams (lsb)
struct BitReaderLSB : ref<byte> {
	size_t index = 0;
	BitReaderLSB(ref<byte> data) : ref<byte>(data) {}
	/// Reads \a size bits
	uint read(uint size) {
		uint value = (*(uint64*)(data+index/8) << (64-size-(index&7))) >> /*int8*/(64-size);
		index += size;
		return value;
	}
	void align() { index = (index + 7) & ~7; }
	ref<byte> readBytes(uint byteCount) {
		assert((index&7) == 0);
		ref<byte> slice = ref<byte>((byte*)data+index/8, byteCount);
		index += byteCount*8;
		return slice;
	}
};

static constexpr size_t EG = 6;

buffer<byte> encode(ref<uint16> source) {
	buffer<byte> buffer(source.size*2);
	BitWriter bitIO {buffer}; // Assumes buffer capacity will be large enough
	int predictor = 0;
	for(size_t index=0;;) {
		if(index >= source.size) break;
		int value = source[index];
		index++;
		int s = value - predictor;
		predictor = value;
		uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
		// Exp Golomb _ r
		uint x = u + (1<<EG);
		uint b = 63 - __builtin_clzl(x); // BSR
		bitIO.write(b+b+1-EG, x); // unary b, binary q[b], binary r = 0^b.1.q[b]R[r] = x[b+b+1+r]
	}
	bitIO.flush();
	buffer.size = (byte*)bitIO.pointer - buffer.data;
	//buffer.size += 7; // No need to pad to avoid page fault with optimized BitReader (whose last refill may stride end) since registers will already pad
	return buffer;
};

buffer<uint16> decode(ref<byte> data, size_t capacity) {
	buffer<uint16> buffer(capacity);
	BitReader bitIO(data);
	int predictor = 0;
	size_t index = 0;
	for(;;) {
		uint u = bitIO.readExpGolomb<EG>();
		int s = (u>>1) ^ (-(u&1)); // u&1 ? -((u>>1) + 1) : u>>1;
		predictor += s;
		buffer[index] = predictor;
		index++;
		if(index>=buffer.size) break;
	}
	return buffer;
}

/// Deinterleaves Bayer cells (for better prediction) and shifts 4 zero least significant bits (for better coding)
buffer<uint16> deinterleave(ref<uint16> source, size_t X, size_t Y, const int shift=4) {
	buffer<uint16> target(source.size);
	mref<uint16> G = target.slice(0, Y*X/2);
	mref<uint16> R = target.slice(Y*X/2, Y/2*X/2);
	mref<uint16> B = target.slice(Y*X/2+Y/2*X/2, Y/2*X/2);
	for(size_t y: range(Y/2)) for(size_t x: range(X/2)) {
		G[(y*2+0)*X/2+x] = source[(y*2+0)*X+(x*2+0)] >> shift;
		R[(y*1+0)*X/2+x] = source[(y*2+0)*X+(x*2+1)] >> shift;
		B[(y*1+0)*X/2+x] = source[(y*2+1)*X+(x*2+0)] >> shift;
		G[(y*2+1)*X/2+x] = source[(y*2+1)*X+(x*2+1)] >> shift;
	}
	return target;
}

/// Reinterleaves Bayer cells and shifts (for compatibility with original raw16 files)
buffer<uint16> interleave(ref<uint16> source, size_t X, size_t Y, const int shift=4) {
	buffer<uint16> target(source.size);
	ref<uint16> G = source.slice(0, Y*X/2);
	ref<uint16> R = source.slice(Y*X/2, Y/2*X/2);
	ref<uint16> B = source.slice(Y*X/2+Y/2*X/2, Y/2*X/2);
	for(size_t y: range(Y/2)) for(size_t x: range(X/2)) {
		target[(y*2+0)*X+(x*2+0)] = G[(y*2+0)*X/2+x] << shift;
		target[(y*2+0)*X+(x*2+1)] = R[(y*1+0)*X/2+x] << shift;
		target[(y*2+1)*X+(x*2+0)] = B[(y*1+0)*X/2+x] << shift;
		target[(y*2+1)*X+(x*2+1)] = G[(y*2+1)*X/2+x] << shift;
	}
	return target;
}

extern "C" int open(const char* file, int flags, int mask); // fcntl.h
#include <sys/stat.h>
extern "C" byte* mmap (void* address, size_t size, int prot, int flags, int fd, long offset); // sys/mman.h
extern "C" int write(int fd, const byte* data, size_t size);
int main(int, const char* args[]) {
	int sourceFD = open(args[1], 0/*O_RDONLY*/, 0);
	struct stat stat; fstat(sourceFD, &stat);
	ref<byte> file ((byte*)mmap(0, stat.st_size, 1/*PROT_READ*/, 1/*MAP_PRIVATE*/, sourceFD, 0), stat.st_size);
	int targetFD = open(args[2], 1/*O_WRONLY*/|0100/*O_CREAT*/|01000/*O_TRUNC*/, 0666);
	size_t X=4096, Y=3072;
	if(file.size == Y*X*2+256) { // Encode
		ref<uint16> source ((uint16*)file.data, Y*X);
		buffer<byte> target = encode(deinterleave(source, X, Y));
		if(interleave(decode(target, Y*X), X, Y) != source) return 1;
		write(targetFD, target.data, target.size);
	} else { // Decode
		buffer<uint16> target = interleave(decode(file, Y*X), X, Y);
		write(targetFD, (byte*)target.data, target.size*2);
	}
	ref<byte> trailer = file.slice(file.size-256, 256);
	write(targetFD, trailer.data, trailer.size);
	return 0;
}
