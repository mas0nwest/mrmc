/*
 *      Copyright (C) 2015 Team MrMC
 *      https://github.com/MrMC
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
*/

#include <stdlib.h>
#include <setjmp.h>

#include "guilib/PngIO.h"
#include "utils/log.h"

#ifndef png_jmpbuf
#  define png_jmpbuf(png_ptr) ((png_ptr)->jmpbuf)
#endif

CPngIO::CPngIO()
  : IImage()
  , pngInfoPtr(NULL)
  , pngStructPtr(NULL)
  , m_compressedPtr(NULL)
  , m_compressedCnt(0)
  , m_compressedSize(0)
  , m_compressedBuffer(NULL)
{
}

CPngIO::~CPngIO()
{
  ReleaseThumbnailBuffer();
}

bool CPngIO::LoadImageFromMemory(unsigned char* buffer, unsigned int bufSize, unsigned int width, unsigned int height)
{
  // buffer will persist, width and height are 1) real size of image or 2) max surface size.
  // we just save params for Decode function
  m_width = width;
  m_height = height;
  // we always have or create an alpha channel
  m_hasAlpha = true;

  m_compressedPtr = buffer;
  m_compressedSize = bufSize;

  if (png_sig_cmp(m_compressedPtr, 0, 8) != 0)
  {
    CLog::Log(LOGERROR, "PngIO: not a PNG");
    return false;
  }
  m_compressedPtr += 8;

  pngStructPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!pngStructPtr)
  {
    CLog::Log(LOGERROR, "PngIO: png_create_read_struct returned 0");
    return false;
  }
  // the code in this if statement gets called if libpng encounters an error
  if (setjmp(png_jmpbuf(pngStructPtr)))
  {
    CLog::Log(LOGERROR, "unknown error from libpng");
    png_destroy_read_struct(&pngStructPtr, &pngInfoPtr, NULL);
    return false;
  }

  // create png info struct
  pngInfoPtr = png_create_info_struct(pngStructPtr);
  if (!pngInfoPtr)
  {
    CLog::Log(LOGERROR, "PngIO: png_create_info_struct returned 0");
    png_destroy_read_struct(&pngStructPtr, (png_infopp)NULL, (png_infopp)NULL);
    return false;
  }

  png_set_read_fn(pngStructPtr, (void*)this, ReadMemoryCallback);

  // let libpng know we already read the first 8 bytes
  png_set_sig_bytes(pngStructPtr, 8);

  // read all the info up to the image data
  png_read_info(pngStructPtr, pngInfoPtr);

  // variables to pass to get info
  int bit_depth, color_type;
  png_uint_32 info_width, info_height;

  // get info about png
  png_get_IHDR(pngStructPtr, pngInfoPtr, &info_width, &info_height,
               &bit_depth, &color_type, NULL, NULL, NULL);

  if (info_width < m_width)
    m_width = info_width;
  if (info_height < m_height)
    m_height = info_height;

  if (info_width != m_originalWidth)
    m_originalWidth = info_width;
  if (info_height != m_originalHeight)
    m_originalHeight = info_height;

  if (png_get_valid(pngStructPtr, pngInfoPtr, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(pngStructPtr);

  // set it to 32bit pixeldepth
  png_color_8 sig_bit;
  sig_bit.red   = 32;
  sig_bit.green = 32;
  sig_bit.blue  = 32;
  // if the image has an alpha channel then
  sig_bit.alpha = 32;
  png_set_sBIT(pngStructPtr, pngInfoPtr, &sig_bit);

  // add filler (or alpha) byte (before/after each RGB triplet)
  png_set_filler(pngStructPtr, 0xff, PNG_FILLER_AFTER);

  if (color_type == PNG_COLOR_TYPE_RGB ||
      color_type == PNG_COLOR_TYPE_RGB_ALPHA)
    png_set_bgr(pngStructPtr);

  // convert indexed color to rgb
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(pngStructPtr);

  // we only eat 32bit RGBA, must convert grayscale into this format
  if (color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
  {
    png_set_expand_gray_1_2_4_to_8(pngStructPtr);
    png_set_gray_to_rgb(pngStructPtr);
  }

  // update the png info struct
  png_read_update_info(pngStructPtr, pngInfoPtr);

  return true;
}

bool CPngIO::Decode(const unsigned char *pixels, unsigned int pitch)
{
  // the code in this if statement gets called if libpng encounters an error
  if (setjmp(png_jmpbuf(pngStructPtr)))
  {
    CLog::Log(LOGERROR, "unknown error from libpng");
    png_destroy_read_struct(&pngStructPtr, &pngInfoPtr, NULL);
    return false;
  }

  // row size in bytes
  unsigned int rowbytes = png_get_rowbytes(pngStructPtr, pngInfoPtr);
  if (rowbytes < pitch)
    rowbytes = pitch;

  // row_pointers is for pointing to image_data for reading the png with libpng
  png_bytep *row_pointers = (png_bytep*)new png_bytep[m_height * sizeof(png_bytep)];
  if (row_pointers == NULL)
  {
    CLog::Log(LOGERROR, "PngIO: could not allocate memory for PNG row pointers");
    png_destroy_read_struct(&pngStructPtr, &pngInfoPtr, NULL);
    return false;
  }

  png_byte *image_data = (png_byte*)pixels;
  // set the individual row_pointers to point at the correct offsets of image_data
  for (unsigned int i = 0; i < m_height; i++)
    row_pointers[i] = image_data + i * rowbytes;

  // read the png into image_data through row_pointers
  png_read_image(pngStructPtr, row_pointers);
  png_read_end(pngStructPtr, pngInfoPtr);
  png_destroy_read_struct(&pngStructPtr, &pngInfoPtr, NULL);

  return true;
}

bool CPngIO::CreateThumbnailFromSurface(unsigned char* bufferin, unsigned int width, unsigned int height,
  unsigned int pitch, const std::string& destFile,
  unsigned char* &bufferout, unsigned int &bufferoutSize)
{ // given a surface, encode it to memory and pass the memory back. someone else will do i/o with it.
  pngStructPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!pngStructPtr)
  {
    CLog::Log(LOGERROR, "PngIO: png_create_write_struct returned 0");
    return false;
  }
  // the code in this if statement gets called if libpng encounters an error
  if (setjmp(png_jmpbuf(pngStructPtr)))
  {
    CLog::Log(LOGERROR, "unknown error from libpng");
    png_destroy_write_struct(&pngStructPtr, NULL);
    return false;
  }

  pngInfoPtr = png_create_info_struct(pngStructPtr);
  if (!pngInfoPtr)
  {
    CLog::Log(LOGERROR, "PngIO: png_create_info_struct returned 0");
    png_destroy_write_struct(&pngStructPtr, (png_infopp)NULL);
    return false;
  }

  png_set_IHDR(pngStructPtr, pngInfoPtr, width, height, 8,
    PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  // esitmate on compressed size, this can grow as needed in WriteMemoryCallback
  m_compressedSize = 2 * width * height;
  m_compressedBuffer = (unsigned char*)malloc(m_compressedSize);
  if (m_compressedBuffer == NULL)
  {
    CLog::Log(LOGERROR, "PngIO::CreateThumbnailFromSurface error allocating memory for image buffer");
    png_destroy_write_struct(&pngStructPtr, &pngInfoPtr);
    return false;
  }
  m_compressedPtr = m_compressedBuffer;

  // row_pointers is for pointing to image_data for reading the png with libpng
  png_bytep *row_pointers = (png_bytep*)new png_bytep[height * sizeof(png_bytep)];
  if (row_pointers == NULL)
  {
    CLog::Log(LOGERROR, "PngIO: could not allocate memory for PNG row pointers");
    png_destroy_write_struct(&pngStructPtr, &pngInfoPtr);
    return false;
  }
  for (unsigned int i = 0; i < height; i++)
    row_pointers[i] = bufferin + i * pitch;
  png_set_rows(pngStructPtr, pngInfoPtr, row_pointers);

  png_set_write_fn(pngStructPtr, (void*)this, WriteMemoryCallback, NULL);
  png_write_png(pngStructPtr, pngInfoPtr, PNG_TRANSFORM_BGR, NULL);
  png_destroy_write_struct(&pngStructPtr, &pngInfoPtr);

  // these are modified in WriteMemoryCallback
  bufferout = m_compressedBuffer;
  bufferoutSize = m_compressedCnt;

  return true;
}

void CPngIO::ReleaseThumbnailBuffer()
{
  m_compressedPtr = NULL;
  m_compressedCnt = 0;
  m_compressedSize = 0;
  if (m_compressedBuffer != NULL)
    free(m_compressedBuffer), m_compressedBuffer = NULL;
}

void CPngIO::ReadMemoryCallback(png_structp png_ptr, png_bytep buffer, png_size_t count)
{
  if (png_ptr == NULL)
    return;

  CPngIO *pngio = (CPngIO*)png_get_io_ptr(png_ptr);
  memcpy(buffer, pngio->m_compressedPtr, count);
  pngio->m_compressedPtr += count;
  pngio->m_compressedCnt += count;
}

void CPngIO::WriteMemoryCallback(png_structp png_ptr, png_bytep buffer, png_size_t count)
{
  if (png_ptr == NULL)
    return;

  CPngIO *pngio = (CPngIO*)png_get_io_ptr(png_ptr);
  if (pngio->m_compressedCnt + count > pngio->m_compressedSize)
  {
    // grow memory buffer by 40k to save on realloc calls
    pngio->m_compressedSize = pngio->m_compressedCnt + count + (4096 * 10);
    pngio->m_compressedBuffer = (unsigned char *)realloc((void*)pngio->m_compressedBuffer, pngio->m_compressedSize);
    pngio->m_compressedPtr = pngio->m_compressedBuffer + pngio->m_compressedCnt;
  }
  memcpy(pngio->m_compressedPtr, buffer, count);
  pngio->m_compressedPtr += count;
  pngio->m_compressedCnt += count;
}