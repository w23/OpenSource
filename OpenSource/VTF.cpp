#include "VTF.h"
#include <Kapusha/sys/System.h>
#include <Kapusha/io/Stream.h>
#include <Kapusha/sys/Log.h>

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

struct Image {
  struct ColorRGB565 {
    u16 c;

    math::vec4f asRGBAf() const
    {
      return math::vec4f((c >> 11) / 31.f,
                         (((c >> 5)) & 63) / 63.f,
                         (c & 31) / 31.f,
                         1.f);
    }
  };

  const int width, height;
  enum Format {
    FormatNone = 0,
    FormatBGRA8888,
    FormatRGB565,
    FormatRGBAf
  };
  const Format format;
  u8 *pixels;

  const static int formatSize[];

  //Image() : width(0), height(0), format(FormatNone), pixels(0) {}
  Image(int _width, int _height, Format _format = FormatBGRA8888)
    : width(_width), height(_height), format(_format)
  {
    KP_ASSERT(format != FormatNone);
    pixels = new u8[width * height * formatSize[format]];
  }

  ~Image()
  {
    delete pixels;
  }

  bool readFromDXT1(kapusha::Stream& stream)
  {
    KP_ASSERT(width > 0);
    KP_ASSERT(height > 0);
    KP_ASSERT(width%4 == 0 || width < 4);
    KP_ASSERT(height%4 == 0 || height < 4);
    KP_ASSERT(format == FormatRGBAf);

    for (int y = 0; y < height; y += 4)
    {
      math::vec4f *p = reinterpret_cast<math::vec4f*>(pixels) + y * width;
      for (int x = 0; x < width; x += 4, p += 4)
      {
        struct {
          ColorRGB565 c[2];
          u8 map[4];
        } chunk;
        //! \fixme this floatness is retarded
        math::vec4f c[4];
        if (Stream::ErrorNone != stream.copy(&chunk, 8))
          return false;
        c[0] = chunk.c[0].asRGBAf();
        c[1] = chunk.c[1].asRGBAf();

        if (chunk.c[0].c > chunk.c[1].c)
        {
          c[2] = (c[0] * 2.f + c[1]) * (1.f / 3.f);
          c[3] = (c[0] + c[1] * 2.f) * (1.f / 3.f);
        } else {
          c[2] = (c[0] + c[1]) * .5f;
          c[3] = math::vec4f(0);
        }

        math::vec4f* pp = p;
        for(int row = 0; row < 4 && ((row+y) < height); ++row, pp += width-4)
        {
          *pp++ = c[chunk.map[row] >> 6];
          if(x+1 < width) *pp = c[(chunk.map[row] >> 4) & 3];
          ++pp;
          if(x+2 < width) *pp = c[(chunk.map[row] >> 2) & 3];
          ++pp;
          if(x+3 < width) *pp = c[chunk.map[row] & 3];
          ++pp;
        }
      }
    }

    return true;
  }
};

const int Image::formatSize[] = {
  0, 4, 2, 16
};

VTF::VTF(void)
{
}

VTF::~VTF(void)
{
}

bool VTF::load(kapusha::Stream& stream)
{
  vtf_file_header_t file_header;
  if(Stream::ErrorNone != stream.copy(&file_header, sizeof file_header))
  {
    return false;
  }
  
  if (file_header.magic[0] != 'V' || file_header.magic[1] != 'T' || 
      file_header.magic[2] != 'F' || file_header.magic[3] != 0)
      return false;

  L("vtf image ver %d.%d", file_header.version[0], file_header.version[1]);
  if (!(file_header.version[0] == 7 && (file_header.version[1] == 0 || file_header.version[1] == 1)))
  {
    L("versions other than 7.0, 7.1 are not supported");
    return false;
  }

  vtf_70_header_t header;
  KP_ENSURE(Stream::ErrorNone == stream.copy(&header, sizeof header));

  L("\timage format %d size %dx%d",
    header.format, header.width, header.height);
  L("\tflags %08x", header.flags);
  L("\tframes %d->%d", header.firstFrame, header.frames);
  L("\treflect %f %f %f, bump %f",
    header.reflectivity[0], header.reflectivity[1], header.reflectivity[2],
    header.bumpmapScale);

  L("\tlowres image format %d size %dx%d",
    header.lowresFormat, header.lowresWidth, header.lowresHeight);
  Image lowres(header.lowresWidth, header.lowresHeight, Image::FormatRGBAf);
  KP_ENSURE(lowres.readFromDXT1(stream));

  average_color_ = math::vec3f(0);
  math::vec4f 
    *p = reinterpret_cast<math::vec4f*>(lowres.pixels),
    *end = p + lowres.width * lowres.height;
  for(; p < end; ++p)
    average_color_ += p->xyz();

  average_color_ *= 1.f / (lowres.width * lowres.height);

  return true;
}