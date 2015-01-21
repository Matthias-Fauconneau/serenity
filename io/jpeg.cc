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
  for(int y : range(jpeg.output_height)) {
	  uint8* line = (uint8*)(image.data+y*jpeg.output_width);
	  jpeg_read_scanlines(&jpeg, &line, 1);
  }
  jpeg_finish_decompress(&jpeg);
  jpeg_destroy_decompress(&jpeg);
  return image;
}
