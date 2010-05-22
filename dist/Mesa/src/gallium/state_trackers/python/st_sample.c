/**************************************************************************
 * 
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/


#include "pipe/p_compiler.h"
#include "pipe/p_format.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "util/u_tile.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include "st_device.h"
#include "st_sample.h"


/**
 * Use our own pseudo random generator to ensure consistent runs among
 * multiple runs and platforms.
 * 
 * @sa http://en.wikipedia.org/wiki/Linear_congruential_generator
 */
static uint32_t st_random(void) {
   static uint64_t seed = UINT64_C(0xbb9a063afb0a739d);

   seed = UINT64_C(134775813) * seed + UINT64_C(1);
   
   return (uint16_t)(seed >> 32); 
}


/**
 * We don't want to include the patent-encumbered DXT code here, so instead
 * we store several uncompressed/compressed data pairs for hardware testing
 * purposes. 
 */
struct dxt_data
{
   uint8_t rgba[16*4];
   uint8_t raw[16];
};


static const struct dxt_data 
dxt1_rgb_data[] = {
   {
      {
         0x99, 0xb0, 0x8e, 0xff,
         0x5d, 0x62, 0x89, 0xff,
         0x99, 0xb0, 0x8e, 0xff,
         0x99, 0xb0, 0x8e, 0xff,
         0xd6, 0xff, 0x94, 0xff,
         0x5d, 0x62, 0x89, 0xff,
         0x99, 0xb0, 0x8e, 0xff,
         0xd6, 0xff, 0x94, 0xff,
         0x5d, 0x62, 0x89, 0xff,
         0x5d, 0x62, 0x89, 0xff,
         0x99, 0xb0, 0x8e, 0xff,
         0x21, 0x14, 0x84, 0xff,
         0x5d, 0x62, 0x89, 0xff,
         0x21, 0x14, 0x84, 0xff,
         0x21, 0x14, 0x84, 0xff,
         0x99, 0xb0, 0x8e, 0xff
      },
      {0xf2, 0xd7, 0xb0, 0x20, 0xae, 0x2c, 0x6f, 0x97}
   },
   {
      {
         0xb5, 0xcf, 0x9c, 0xff,
         0x83, 0x8c, 0x8b, 0xff,
         0x21, 0x08, 0x6b, 0xff,
         0x83, 0x8c, 0x8b, 0xff,
         0x52, 0x4a, 0x7b, 0xff,
         0x83, 0x8c, 0x8b, 0xff,
         0x83, 0x8c, 0x8b, 0xff,
         0xb5, 0xcf, 0x9c, 0xff,
         0x21, 0x08, 0x6b, 0xff,
         0xb5, 0xcf, 0x9c, 0xff,
         0x83, 0x8c, 0x8b, 0xff,
         0x52, 0x4a, 0x7b, 0xff,
         0xb5, 0xcf, 0x9c, 0xff,
         0x83, 0x8c, 0x8b, 0xff,
         0x52, 0x4a, 0x7b, 0xff,
         0x83, 0x8c, 0x8b, 0xff
      },
      {0x73, 0xb6, 0x4d, 0x20, 0x98, 0x2b, 0xe1, 0xb8}
   },
   {
      {
         0x00, 0x2c, 0xff, 0xff,
         0x94, 0x8d, 0x7b, 0xff,
         0x4a, 0x5c, 0xbd, 0xff,
         0x4a, 0x5c, 0xbd, 0xff,
         0x4a, 0x5c, 0xbd, 0xff,
         0x94, 0x8d, 0x7b, 0xff,
         0x94, 0x8d, 0x7b, 0xff,
         0x94, 0x8d, 0x7b, 0xff,
         0xde, 0xbe, 0x39, 0xff,
         0x94, 0x8d, 0x7b, 0xff,
         0xde, 0xbe, 0x39, 0xff,
         0xde, 0xbe, 0x39, 0xff,
         0xde, 0xbe, 0x39, 0xff,
         0xde, 0xbe, 0x39, 0xff,
         0xde, 0xbe, 0x39, 0xff,
         0x94, 0x8d, 0x7b, 0xff
      },
      {0xe7, 0xdd, 0x7f, 0x01, 0xf9, 0xab, 0x08, 0x80}
   },
   {
      {
         0x6b, 0x24, 0x21, 0xff,
         0x7b, 0x4f, 0x5d, 0xff,
         0x7b, 0x4f, 0x5d, 0xff,
         0x8b, 0x7a, 0x99, 0xff,
         0x7b, 0x4f, 0x5d, 0xff,
         0x7b, 0x4f, 0x5d, 0xff,
         0x6b, 0x24, 0x21, 0xff,
         0x8b, 0x7a, 0x99, 0xff,
         0x9c, 0xa6, 0xd6, 0xff,
         0x6b, 0x24, 0x21, 0xff,
         0x7b, 0x4f, 0x5d, 0xff,
         0x8b, 0x7a, 0x99, 0xff,
         0x6b, 0x24, 0x21, 0xff,
         0x8b, 0x7a, 0x99, 0xff,
         0x7b, 0x4f, 0x5d, 0xff,
         0x9c, 0xa6, 0xd6, 0xff
      },
      {0x3a, 0x9d, 0x24, 0x69, 0xbd, 0x9f, 0xb4, 0x39}
   }
};


static const struct dxt_data 
dxt1_rgba_data[] = {
   {
      {
         0x00, 0x00, 0x00, 0x00,
         0x4e, 0xaa, 0x90, 0xff,
         0x4e, 0xaa, 0x90, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x4e, 0xaa, 0x90, 0xff,
         0x29, 0xff, 0xff, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x4e, 0xaa, 0x90, 0xff,
         0x73, 0x55, 0x21, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x4e, 0xaa, 0x90, 0xff,
         0x4e, 0xaa, 0x90, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x4e, 0xaa, 0x90, 0xff
      },
      {0xff, 0x2f, 0xa4, 0x72, 0xeb, 0xb2, 0xbd, 0xbe}
   },
   {
      {
         0xb5, 0xe3, 0x63, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x6b, 0x24, 0x84, 0xff,
         0xb5, 0xe3, 0x63, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0xb5, 0xe3, 0x63, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x6b, 0x24, 0x84, 0xff,
         0x6b, 0x24, 0x84, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0xb5, 0xe3, 0x63, 0xff,
         0x90, 0x83, 0x73, 0xff,
         0xb5, 0xe3, 0x63, 0xff
      },
      {0x30, 0x69, 0x0c, 0xb7, 0x4d, 0xf7, 0x0f, 0x67}
   },
   {
      {
         0x00, 0x00, 0x00, 0x00,
         0xc6, 0x86, 0x8c, 0xff,
         0xc6, 0x86, 0x8c, 0xff,
         0x21, 0x65, 0x42, 0xff,
         0x21, 0x65, 0x42, 0xff,
         0x21, 0x65, 0x42, 0xff,
         0x21, 0x65, 0x42, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x21, 0x65, 0x42, 0xff,
         0xc6, 0x86, 0x8c, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0xc6, 0x86, 0x8c, 0xff
      },
      {0x28, 0x23, 0x31, 0xc4, 0x17, 0xc0, 0xd3, 0x7f}
   },
   {
      {
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0xc6, 0xe3, 0x9c, 0xff,
         0x7b, 0x1c, 0x52, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00,
         0x7b, 0x1c, 0x52, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0x7b, 0x1c, 0x52, 0xff,
         0xa0, 0x7f, 0x77, 0xff,
         0xc6, 0xe3, 0x9c, 0xff,
         0x00, 0x00, 0x00, 0x00,
         0xa0, 0x7f, 0x77, 0xff
      },
      {0xea, 0x78, 0x13, 0xc7, 0x7f, 0xfc, 0x33, 0xb6}
   },
};


static const struct dxt_data 
dxt3_rgba_data[] = {
   {
      {
         0x6d, 0xc6, 0x96, 0x77,
         0x6d, 0xc6, 0x96, 0xee,
         0x6d, 0xc6, 0x96, 0xaa,
         0x8c, 0xff, 0xb5, 0x44,
         0x6d, 0xc6, 0x96, 0xff,
         0x6d, 0xc6, 0x96, 0x88,
         0x31, 0x55, 0x5a, 0x66,
         0x6d, 0xc6, 0x96, 0x99,
         0x31, 0x55, 0x5a, 0xbb,
         0x31, 0x55, 0x5a, 0x55,
         0x31, 0x55, 0x5a, 0x11,
         0x6d, 0xc6, 0x96, 0xcc,
         0x6d, 0xc6, 0x96, 0xcc,
         0x6d, 0xc6, 0x96, 0x11,
         0x31, 0x55, 0x5a, 0x44,
         0x31, 0x55, 0x5a, 0x88
      },
      {0xe7, 0x4a, 0x8f, 0x96, 0x5b, 0xc1, 0x1c, 0x84, 0xf6, 0x8f, 0xab, 0x32, 0x2a, 0x9a, 0x95, 0x5a}
   },
   {
      {
         0xad, 0xeb, 0x73, 0x99,
         0x97, 0xaa, 0x86, 0x66,
         0x6b, 0x28, 0xad, 0x99,
         0xad, 0xeb, 0x73, 0x99,
         0x6b, 0x28, 0xad, 0x22,
         0xad, 0xeb, 0x73, 0xff,
         0x97, 0xaa, 0x86, 0x55,
         0x6b, 0x28, 0xad, 0x55,
         0x6b, 0x28, 0xad, 0x44,
         0xad, 0xeb, 0x73, 0x33,
         0x6b, 0x28, 0xad, 0xee,
         0x6b, 0x28, 0xad, 0x99,
         0x97, 0xaa, 0x86, 0x66,
         0xad, 0xeb, 0x73, 0xbb,
         0x97, 0xaa, 0x86, 0x99,
         0xad, 0xeb, 0x73, 0xbb
      },
      {0x69, 0x99, 0xf2, 0x55, 0x34, 0x9e, 0xb6, 0xb9, 0x4e, 0xaf, 0x55, 0x69, 0x18, 0x61, 0x51, 0x22}
   },
   {
      {
         0x63, 0xd7, 0xd6, 0x00,
         0x57, 0x62, 0x5d, 0xdd,
         0x57, 0x62, 0x5d, 0xcc,
         0x57, 0x62, 0x5d, 0xbb,
         0x52, 0x28, 0x21, 0xaa,
         0x57, 0x62, 0x5d, 0xcc,
         0x57, 0x62, 0x5d, 0xcc,
         0x57, 0x62, 0x5d, 0x66,
         0x57, 0x62, 0x5d, 0x22,
         0x57, 0x62, 0x5d, 0xdd,
         0x63, 0xd7, 0xd6, 0xee,
         0x57, 0x62, 0x5d, 0x33,
         0x63, 0xd7, 0xd6, 0x55,
         0x52, 0x28, 0x21, 0x55,
         0x57, 0x62, 0x5d, 0x11,
         0x5d, 0x9c, 0x99, 0xee
      },
      {0xd0, 0xbc, 0xca, 0x6c, 0xd2, 0x3e, 0x55, 0xe1, 0xba, 0x66, 0x44, 0x51, 0xfc, 0xfd, 0xcf, 0xb4}
   },
   {
      {
         0x94, 0x6f, 0x60, 0x22,
         0x94, 0x6f, 0x60, 0x22,
         0xc5, 0xab, 0x76, 0x11,
         0xc5, 0xab, 0x76, 0xee,
         0x63, 0x34, 0x4a, 0xdd,
         0x63, 0x34, 0x4a, 0x33,
         0x94, 0x6f, 0x60, 0x77,
         0xf7, 0xe7, 0x8c, 0x00,
         0x94, 0x6f, 0x60, 0x33,
         0x63, 0x34, 0x4a, 0xaa,
         0x94, 0x6f, 0x60, 0x77,
         0x63, 0x34, 0x4a, 0xcc,
         0x94, 0x6f, 0x60, 0xaa,
         0xf7, 0xe7, 0x8c, 0x99,
         0x63, 0x34, 0x4a, 0x44,
         0xc5, 0xab, 0x76, 0xaa
      },
      {0x22, 0xe1, 0x3d, 0x07, 0xa3, 0xc7, 0x9a, 0xa4, 0x31, 0xf7, 0xa9, 0x61, 0xaf, 0x35, 0x77, 0x93}
   },
};


static const struct dxt_data 
dxt5_rgba_data[] = {
   {
      {
         0x6d, 0xc6, 0x96, 0x74,
         0x6d, 0xc6, 0x96, 0xf8,
         0x6d, 0xc6, 0x96, 0xb6,
         0x8c, 0xff, 0xb5, 0x53,
         0x6d, 0xc6, 0x96, 0xf8,
         0x6d, 0xc6, 0x96, 0x95,
         0x31, 0x55, 0x5a, 0x53,
         0x6d, 0xc6, 0x96, 0x95,
         0x31, 0x55, 0x5a, 0xb6,
         0x31, 0x55, 0x5a, 0x53,
         0x31, 0x55, 0x5a, 0x11,
         0x6d, 0xc6, 0x96, 0xd7,
         0x6d, 0xc6, 0x96, 0xb6,
         0x6d, 0xc6, 0x96, 0x11,
         0x31, 0x55, 0x5a, 0x32,
         0x31, 0x55, 0x5a, 0x95
      },
      {0xf8, 0x11, 0xc5, 0x0c, 0x9a, 0x73, 0xb4, 0x9c, 0xf6, 0x8f, 0xab, 0x32, 0x2a, 0x9a, 0x95, 0x5a}
   },
   {
      {
         0xad, 0xeb, 0x73, 0xa1,
         0x97, 0xaa, 0x86, 0x65,
         0x6b, 0x28, 0xad, 0xa1,
         0xad, 0xeb, 0x73, 0xa1,
         0x6b, 0x28, 0xad, 0x2a,
         0xad, 0xeb, 0x73, 0xfb,
         0x97, 0xaa, 0x86, 0x47,
         0x6b, 0x28, 0xad, 0x65,
         0x6b, 0x28, 0xad, 0x47,
         0xad, 0xeb, 0x73, 0x47,
         0x6b, 0x28, 0xad, 0xdd,
         0x6b, 0x28, 0xad, 0xa1,
         0x97, 0xaa, 0x86, 0x65,
         0xad, 0xeb, 0x73, 0xbf,
         0x97, 0xaa, 0x86, 0xa1,
         0xad, 0xeb, 0x73, 0xbf
      },
      {0xfb, 0x2a, 0x34, 0x19, 0xdc, 0xbf, 0xe8, 0x71, 0x4e, 0xaf, 0x55, 0x69, 0x18, 0x61, 0x51, 0x22}
   },
   {
      {
         0x63, 0xd7, 0xd6, 0x00,
         0x57, 0x62, 0x5d, 0xf5,
         0x57, 0x62, 0x5d, 0xd2,
         0x57, 0x62, 0x5d, 0xaf,
         0x52, 0x28, 0x21, 0xaf,
         0x57, 0x62, 0x5d, 0xd2,
         0x57, 0x62, 0x5d, 0xd2,
         0x57, 0x62, 0x5d, 0x69,
         0x57, 0x62, 0x5d, 0x23,
         0x57, 0x62, 0x5d, 0xd2,
         0x63, 0xd7, 0xd6, 0xf5,
         0x57, 0x62, 0x5d, 0x46,
         0x63, 0xd7, 0xd6, 0x46,
         0x52, 0x28, 0x21, 0x69,
         0x57, 0x62, 0x5d, 0x23,
         0x5d, 0x9c, 0x99, 0xf5
      },
      {0xf5, 0x00, 0x81, 0x36, 0xa9, 0x17, 0xec, 0x1e, 0xba, 0x66, 0x44, 0x51, 0xfc, 0xfd, 0xcf, 0xb4}
   },
   {
      {
         0x94, 0x6f, 0x60, 0x25,
         0x94, 0x6f, 0x60, 0x25,
         0xc5, 0xab, 0x76, 0x05,
         0xc5, 0xab, 0x76, 0xe8,
         0x63, 0x34, 0x4a, 0xe8,
         0x63, 0x34, 0x4a, 0x25,
         0x94, 0x6f, 0x60, 0x86,
         0xf7, 0xe7, 0x8c, 0x05,
         0x94, 0x6f, 0x60, 0x25,
         0x63, 0x34, 0x4a, 0xa7,
         0x94, 0x6f, 0x60, 0x66,
         0x63, 0x34, 0x4a, 0xc7,
         0x94, 0x6f, 0x60, 0xa7,
         0xf7, 0xe7, 0x8c, 0xa7,
         0x63, 0x34, 0x4a, 0x45,
         0xc5, 0xab, 0x76, 0xa7
      },
      {0xe8, 0x05, 0x7f, 0x80, 0x33, 0x5f, 0xb5, 0x79, 0x31, 0xf7, 0xa9, 0x61, 0xaf, 0x35, 0x77, 0x93}
   },
};


static INLINE void 
st_sample_dxt_pixel_block(enum pipe_format format, 
                          uint8_t *raw,
                          float *rgba, unsigned rgba_stride, 
                          unsigned w, unsigned h)
{
   const struct dxt_data *data;
   unsigned n;
   unsigned i;
   unsigned x, y, ch;
   
   switch(format) {
   case PIPE_FORMAT_DXT1_RGB:
      data = dxt1_rgb_data;
      n = sizeof(dxt1_rgb_data)/sizeof(dxt1_rgb_data[0]);
      break;
   case PIPE_FORMAT_DXT1_RGBA:
      data = dxt1_rgba_data;
      n = sizeof(dxt1_rgba_data)/sizeof(dxt1_rgba_data[0]);
      break;
   case PIPE_FORMAT_DXT3_RGBA:
      data = dxt3_rgba_data;
      n = sizeof(dxt3_rgba_data)/sizeof(dxt3_rgba_data[0]);
      break;
   case PIPE_FORMAT_DXT5_RGBA:
      data = dxt5_rgba_data;
      n = sizeof(dxt5_rgba_data)/sizeof(dxt5_rgba_data[0]);
      break;
   default:
      assert(0);
      return;
   }
   
   i = st_random() % n;
   
   for(y = 0; y < h; ++y)
      for(x = 0; x < w; ++x)
         for(ch = 0; ch < 4; ++ch)
            rgba[y*rgba_stride + x*4 + ch] = (float)(data[i].rgba[y*4*4 + x*4 + ch])/255.0f;
   
   memcpy(raw, data[i].raw, util_format_get_blocksize(format));
}


static INLINE void 
st_sample_generic_pixel_block(enum pipe_format format, 
                              uint8_t *raw,
                              float *rgba, unsigned rgba_stride,
                              unsigned w, unsigned h)
{
   unsigned i;
   unsigned x, y, ch;
   int blocksize = util_format_get_blocksize(format);
   
   for(i = 0; i < blocksize; ++i)
      raw[i] = (uint8_t)st_random();
   
   
   pipe_tile_raw_to_rgba(format,
                         raw,
                         w, h,
                         rgba, rgba_stride);
 
   if(format == PIPE_FORMAT_UYVY || format == PIPE_FORMAT_YUYV) {
      for(y = 0; y < h; ++y) {
         for(x = 0; x < w; ++x) {
            for(ch = 0; ch < 4; ++ch) {
               unsigned offset = y*rgba_stride + x*4 + ch;
               rgba[offset] = CLAMP(rgba[offset], 0.0f, 1.0f);
            }
         }
      }
   }
}


/**
 * Randomly sample pixels.
 */
void 
st_sample_pixel_block(enum pipe_format format,
                      void *raw,
                      float *rgba, unsigned rgba_stride,
                      unsigned w, unsigned h)
{
   switch(format) {
   case PIPE_FORMAT_DXT1_RGB:
   case PIPE_FORMAT_DXT1_RGBA:
   case PIPE_FORMAT_DXT3_RGBA:
   case PIPE_FORMAT_DXT5_RGBA:
      st_sample_dxt_pixel_block(format, raw, rgba, rgba_stride, w, h);
      break;

   default:
      st_sample_generic_pixel_block(format, raw, rgba, rgba_stride, w, h);
      break;
   }
}


void
st_sample_surface(struct st_surface *surface, float *rgba) 
{
   struct pipe_texture *texture = surface->texture;
   struct pipe_screen *screen = texture->screen;
   unsigned width = u_minify(texture->width0, surface->level);
   unsigned height = u_minify(texture->height0, surface->level);
   uint rgba_stride = width * 4;
   struct pipe_transfer *transfer;
   void *raw;

   transfer = screen->get_tex_transfer(screen,
                                       surface->texture,
                                       surface->face,
                                       surface->level,
                                       surface->zslice,
                                       PIPE_TRANSFER_WRITE,
                                       0, 0,
                                       width,
                                       height);
   if (!transfer)
      return;

   raw = screen->transfer_map(screen, transfer);
   if (raw) {
      enum pipe_format format = texture->format;
      uint x, y;
      int nblocksx = util_format_get_nblocksx(format, width);
      int nblocksy = util_format_get_nblocksy(format, height);
      int blockwidth = util_format_get_blockwidth(format);
      int blockheight = util_format_get_blockheight(format);
      int blocksize = util_format_get_blocksize(format);


      for (y = 0; y < nblocksy; ++y) {
         for (x = 0; x < nblocksx; ++x) {
            st_sample_pixel_block(format,
                                  (uint8_t *) raw + y * transfer->stride + x * blocksize,
                                  rgba + y * blockheight * rgba_stride + x * blockwidth * 4,
                                  rgba_stride,
                                  MIN2(blockwidth, width - x*blockwidth),
                                  MIN2(blockheight, height - y*blockheight));
         }
      }

      screen->transfer_unmap(screen, transfer);
   }
   
   screen->tex_transfer_destroy(transfer);
}
