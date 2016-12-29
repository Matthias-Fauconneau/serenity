#pragma once
#include "memory.h"

inline buffer<uint8> encodeRunLength(ref<uint8> source) {
 buffer<uint8> encoded (source.size, 0);
 for(uint i=0;;) {
  if(i==source.size) break;
  {
   int length = 0;
   while(length < 128 && i+length < source.size && (i+length+1 == source.size || source[i+length+1] != source[i+length])) length++;
   if(length) {
    encoded.append(length-1);
    encoded.append(source.slice(i, length));
    i+=length;
    if(length == 128) continue;
   }
  }
  if(i==source.size) break;
  {
   int value = source[i];
   int length = 1;
   while(length < 129 && i+length < source.size && source[i+length] == value) length++;
   assert_(length >= 2, i, source.size, source[i], source[i+1], length);
   /*if(length >= 2)*/ { encoded.append(257-length); encoded.append(value); i+=length; }
  }
 }
 return encoded;
}

inline buffer<uint8> decodeRunLength(ref<uint8> source) {
 buffer<uint8> buffer (source.size*10, 0);
 BinaryData s (cast<byte>(source));
 for(;s;) {
  uint8 code = (uint8)s.next();
  if(code < 128) buffer.append( s.read<uint8>(code+1) );
  else {
   uint8 value = s.next();
   uint size = 257-code;
   for(uint unused i: range(size)) buffer.append( value );
  }
 }
 return buffer;
}

inline buffer<byte> decodeRunLengthPDF(ref<byte> source) {
 array<byte> buffer (source.size);
 Data s (source);
 for(;;) {
  assert_(s);
  uint8 code = s.next();
  if(code < 128) buffer.append( s.read(code+1) );
  else if(code != 128) {
   byte value = s.next();
   uint size = 257-code;
   buffer.reserve(buffer.size+size);
   for(uint unused i: range(size)) buffer.append( value );
  }
  else break;
 }
 return move(buffer);
}
