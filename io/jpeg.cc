#include "jpeg.h"
struct FILE;
#include <jpeglib.h> //jpeg

Image decodeJPEG(const ref<byte> file) {
  jpeg_decompress_struct jpeg;
  jpeg_error_mgr err; jpeg.err = jpeg_std_error(&err);
  jpeg_create_decompress(&jpeg);
  jpeg_mem_src(&jpeg, (uint8*)file.data, file.size);
  jpeg_read_header(&jpeg, TRUE);
  jpeg.out_color_space = JCS_EXT_BGRX;
  jpeg_start_decompress(&jpeg);
  assert_(jpeg.output_components == 4);
  Image image (jpeg.output_width, jpeg.output_height);
  for(int y : range(image.size.y)) {
   uint8* line = (uint8*)(image.data+y*image.size.x);
	  jpeg_read_scanlines(&jpeg, &line, 1);
  }
  jpeg_finish_decompress(&jpeg);
  jpeg_destroy_decompress(&jpeg);
  return image;
}

buffer<byte> encodeJPEG(const Image& image, int quality) {
  struct jpeg_compress_struct jpeg;
  struct jpeg_error_mgr jerr;
  jpeg.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&jpeg);
  buffer<byte> file (image.size.y*image.size.x); // Assumes 3x compression
  jpeg_mem_dest(&jpeg, &(uint8*&)file.data, &file.size);
  jpeg.image_width = image.size.x;
  jpeg.image_height = image.size.y;
  jpeg.input_components = 4;
  jpeg.in_color_space = JCS_EXT_BGRX;
  jpeg_set_defaults(&jpeg);
  jpeg_set_quality(&jpeg, quality, TRUE);
  jpeg_start_compress(&jpeg, TRUE);
  for(size_t y: range(image.size.y)) {
   uint8* line = (uint8*)(image.data+y*image.size.x);
	  jpeg_write_scanlines(&jpeg, &line, 1);
  }
  jpeg_finish_compress(&jpeg);
  jpeg_destroy_compress(&jpeg);
  return file;
}
