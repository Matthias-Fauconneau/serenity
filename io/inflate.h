#pragma once
#include "memory.h"
#include "bit.h"

generic const T& raw(ref<byte> data) { assert_(data.size==sizeof(T)); return *(T*)data.data; }

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

buffer<byte> inflate(ref<byte> compressed, buffer<byte>&& target) { //inflate huffman encoded data
	BitReaderLSB bitIO(compressed);
	size_t targetIndex = 0;
	for(;;) {
		struct Huffman {
			uint count[16] = {};
			uint lookup[288] = {}; // Maximum DEFLATE alphabet size
			Huffman() {}
			// Computes explicit Huffman tree defined implicitly by code lengths
			Huffman(ref<uint> lengths) {
				// Counts the number of codes for each code length
				for(uint length: lengths) if(length) count[length]++;
				// Initializes cumulative offset
				uint offsets[16];
				{int sum = 0; for(size_t i: range(16)) { offsets[i] = sum; sum += count[i]; }}
				// Evaluates code -> symbol translation table
				for(size_t symbol: range(lengths.size)) if(lengths[symbol]) lookup[offsets[lengths[symbol]]++] = symbol;
			}
			uint decode(BitReaderLSB& bitIO) {
				int code=0; uint length=0, sum=0;
				do { // gets more bits while code value is above sum
					code <<= 1;
					code |= bitIO.read(1);
					length += 1;
					sum += count[length];
					code -= count[length];
				} while (code >= 0);
				return lookup[sum + code];
			}
		};
		Huffman literal;
		Huffman distance;

		bool BFINAL = bitIO.read(1);
		int BTYPE = bitIO.read(2);
		if(BTYPE==0) { // Literal
			bitIO.align();
			uint16 length = bitIO.read(16);
			uint16 unused nlength = bitIO.read(16);
			ref<byte> source = bitIO.readBytes(length);
			for(size_t i: range(length)) target[targetIndex+i] = source[i];
			targetIndex += length;
		} else { // Compressed
			if(BTYPE==1) { // Static Huffman codes
				mref<uint>(literal.count).copy({0,0,0,0,0,0,24,24+144,112});
				for(int i: range(24)) literal.lookup[i] = 256+i; // 256-279: 7
				for(int i: range(144)) literal.lookup[24+i] = i; // 0-143: 8
				for(int i: range(8)) literal.lookup[24+144+i] = 256+24+i; // 280-287 : 8
				for(int i: range(112)) literal.lookup[24+144+8+i] = 144+i; // 144-255: 9
				mref<uint>(distance.count).copy({0,0,0,0,0,32});
				for(int i: range(32)) distance.lookup[i] = i; // 0-32: 5
			} else if(BTYPE==2) { // Dynamic Huffman codes
				uint HLITT = 257+bitIO.read(5);
				uint HDIST = 1+bitIO.read(5);
				uint HCLEN = 4+bitIO.read(4); // Code length
				uint codeLengths[19] = {}; // Code lengths to encode codes
				for(uint i: range(HCLEN)) codeLengths[ref<uint>{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15}[i]] = bitIO.read(3);
				Huffman code = ref<uint>(codeLengths);
				// Decodes litteral and distance trees
				uint lengths[HLITT+HDIST]; // Code lengths
				for(uint i=0; i<HLITT+HDIST;) {
					uint symbol = code.decode(bitIO);
					if(symbol < 16) lengths[i++] = symbol;
					else if(symbol == 16) { // Repeats last
						uint last = lengths[i-1];
						for(int unused t: range(3+bitIO.read(2))) lengths[i++] = last;
					}
					else if(symbol == 17) for(int unused t: range(3+bitIO.read(3))) lengths[i++] = 0; // Repeats zeroes [3bit]
					else if(symbol == 18) for(int unused t: range(11+bitIO.read(7))) lengths[i++] = 0; // Repeats zeroes [3bit]
					else error(symbol);
				}
				literal = ref<uint>(lengths, HLITT);
				distance = ref<uint>(lengths+HLITT, HDIST);
			} else error("Reserved BTYPE", BTYPE);

			for(;;) {
				uint symbol = literal.decode(bitIO);
				if(symbol < 256) target[targetIndex++] = symbol; // Literal
				else if(symbol == 256) break; // Block end
				else if(symbol < 286) { // Length-distance
					uint length = 0;
					if(symbol==285) length = 258;
					else {
						uint code[7] = {257, 265, 269, 273, 277, 281, 285};
						uint lengths[6] = {3, 11, 19, 35, 67, 131};
						for(int i: range(6)) if(symbol < code[i+1]) { length = ((symbol-code[i])<<i)+lengths[i]+(i?bitIO.read(i):0); break; }
					}
					uint distanceSymbol = distance.decode(bitIO);
					uint code[15] = {0, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30};
					uint lengths[14] = {1, 5, 9, 17, 33, 65, 129, 257, 513, 1025, 2049, 4097, 8193, 16385};
					uint distance = 0; for(int i: range(14)) if(distanceSymbol < code[i+1]) { distance = ((distanceSymbol-code[i])<<i)+lengths[i]+(i?bitIO.read(i):0); break; }
					for(size_t i : range(length)) target[targetIndex+i] = target[targetIndex-distance+i]; // length may be larger than distance
					targetIndex += length;
				} else error(symbol);
			}
		}
		if(BFINAL) break;
	}
	return move(target);
}
