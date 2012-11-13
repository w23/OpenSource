#include "VTF.h"
#include <Kapusha/sys/System.h>
#include <Kapusha/sys/Log.h>
#include <Kapusha/io/Stream.h>
#include <Kapusha/gl/Texture.h>

using namespace kapusha;

#pragma pack(push)
#pragma pack(1)

struct vtf_file_header_t {
  char magic[4];
  u32 version[2];
  u32 headerSize;
};

struct vtf_70_header_t {
  u16 width;
  u16 height;
  u32 flags;
  u16 frames;
  u16 firstFrame;
  u8 pad0[4];
  float reflectivity[3];
  u8 pad1[4];
  float bumpmapScale;
  u32 format;
  u8 mipmaps;
  u32 lowresFormat;
  u8 lowresWidth;
  u8 lowresHeight;
  u8 pad2;

  enum Format
  {
	  FormatNone = -1,
  	FormatRGBA8888 = 0,
	  FormatABGR8888,
  	FormatRGB888,
	  FormatBGR888,
  	FormatRGB565,
	  FormatI8,
  	FormatIA88,
	  FormatP8,
  	FormatA8,
	  FormatRGB888Blue,
  	FormatBGR888Blue,
	  FormatARGB8888,
  	FormatBGRA8888,
	  FormatDXT1,
  	FormatDXT3,
	  FormatDXT5,
  	FormatBGRX8888,
	  FormatBGR565,
  	FormatBGRX5551,
   	FormatBGRA4444,
	  FormatDXT1Alpha,
  	FormatBGRA5551,
	  FormatUV88,
	  FormatUVWQ8888,
  	FormatRGBA16161616F,
	  FormatRGBA16161616,
	  FormatUVLX8888
  };
};

#pragma pack(pop)

struct Image : public kapusha::Texture::ImageDesc {
  u8 *pixels;

  //Image() : width(0), height(0), format(FormatNone), pixels(0) {}
  Image(int _width, int _height)
    : ImageDesc(_width, _height, Format_BGRA32)
  {
    pixels = new u8[size.x * size.y * 4];
  }

  ~Image()
  {
    delete pixels;
  }

  struct ColorRGB565 {
    u16 c;
    static const int maskRed = 0xf800;
    static const int maskGreen = 0x07e0;
    static const int maskBlue = 0x001f;
    math::vec4f asRGBAf() const
    {
      return math::vec4f((c >> 11) / 31.f,
                         (((c >> 5)) & 63) / 63.f,
                         (c & 31) / 31.f,
                         1.f);
    }
    u32 asBGRA32() const {
      u32 c32 = c;
      //return 0xff | (c32 & 0xf8) | ((c32 << 13) & 0xfc) | ((c32 << 27) & 0xf8);
      return 0xff000000 | ((c32 & maskRed) << 8) | ((c32 & maskGreen) << 5) | ((c32 & maskBlue) << 3);
    }
    static ColorRGB565 weightedSum(ColorRGB565 a, ColorRGB565 b,
                                   int A, int B, int D)
    {
      u32 
        aRB = a.c & (maskRed | maskBlue),
        aG = a.c & maskGreen,
        bRB = b.c & (maskRed | maskBlue),
        bG = b.c & maskGreen;
      //! \todo overflow check? shouldn't happen with A,B,D used here
      aRB = ((A * aRB + B * bRB) / D) & (maskRed | maskBlue);
      aG = ((A * aG + B * bG) / D) & maskGreen;

      ColorRGB565 ret;
      ret.c = aRB | aG;
      return ret;
    }
  };


  bool readFromDXT1(kapusha::Stream& stream)
  {
    KP_ASSERT(size.x > 0);
    KP_ASSERT(size.y > 0);
    KP_ASSERT(format == Format_BGRA32);

    for (int y = 0; y < size.y; y += 4)
    {
      u32 *p = reinterpret_cast<u32*>(pixels) + y * size.x;
      for (int x = 0; x < size.x; x += 4, p += 4)
      {
        struct {
          ColorRGB565 c[2];
          u8 map[4];
        } chunk;
        u32 c[4];
        if (Stream::ErrorNone != stream.copy(&chunk, 8))
          return false;
        c[0] = chunk.c[0].asBGRA32();
        c[1] = chunk.c[1].asBGRA32();

        if (chunk.c[0].c > chunk.c[1].c)
        {
          c[2] = ColorRGB565::weightedSum(chunk.c[0], chunk.c[1], 
                                          2, 1, 3).asBGRA32();
          c[3] = ColorRGB565::weightedSum(chunk.c[0], chunk.c[1],
                                          1, 2, 3).asBGRA32();
        } else {
          c[2] = ColorRGB565::weightedSum(chunk.c[0], chunk.c[1],
                                          1, 1, 2).asBGRA32();
          c[3] = 0;
        }

        u32* pp = p;
        for(int row = 0; row < 4 && ((row+y) < size.y); ++row, pp += size.x-4)
        {
          *pp++ = c[chunk.map[row] >> 6];
          if(x+1 < size.x) *pp = c[(chunk.map[row] >> 4) & 3];
          ++pp;
          if(x+2 < size.x) *pp = c[(chunk.map[row] >> 2) & 3];
          ++pp;
          if(x+3 < size.x) *pp = c[chunk.map[row] & 3];
          ++pp;
        }
      }
    }

    return true;
  }
};

VTF::VTF(void)
{
}

VTF::~VTF(void)
{
}

kapusha::Texture *VTF::load(kapusha::Stream& stream)
{
  vtf_file_header_t file_header;
  if(Stream::ErrorNone != stream.copy(&file_header, sizeof file_header))
  {
    return 0;
  }
  
  if (file_header.magic[0] != 'V' || file_header.magic[1] != 'T' || 
      file_header.magic[2] != 'F' || file_header.magic[3] != 0)
      return 0;

  L("vtf image ver %d.%d", file_header.version[0], file_header.version[1]);
  if (!(file_header.version[0] == 7 && (file_header.version[1] == 0 || file_header.version[1] == 1)))
  {
    L("versions other than 7.0, 7.1 are not supported");
    return 0;
  }

  vtf_70_header_t header;
  int szh = sizeof(header), szfh = sizeof(file_header);
  L("%d %d", szh, szfh);
  KP_ENSURE(Stream::ErrorNone == stream.copy(&header, sizeof header));
  KP_ASSERT(file_header.headerSize == (sizeof(header) + sizeof(file_header)));

  L("\timage format %d size %dx%d",
    header.format, header.width, header.height);
  L("\tflags %08x", header.flags);
  L("\tframes %d->%d", header.firstFrame, header.frames);
  L("\treflect %f %f %f, bump %f",
    header.reflectivity[0], header.reflectivity[1], header.reflectivity[2],
    header.bumpmapScale);

  L("\tlowres image format %d size %dx%d",
    header.lowresFormat, header.lowresWidth, header.lowresHeight);
  KP_ASSERT(header.lowresFormat == vtf_70_header_t::FormatDXT1);
  Image lowres(header.lowresWidth, header.lowresHeight);
  KP_ENSURE(lowres.readFromDXT1(stream));

  size_.x = header.width;
  size_.y = header.height;

  kapusha::Texture *ret = new kapusha::Texture();
  ret->upload(lowres, lowres.pixels);

  return ret;
}