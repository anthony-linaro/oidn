// Copyright 2009-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "image.h"

namespace oidn {

  ImageDesc::ImageDesc(Format format, size_t width, size_t height, size_t pixelByteStride, size_t rowByteStride)
    : width(width),
      height(height),
      format(format)
  {
    if (width > maxDim || height > maxDim || width * height * getC() > std::numeric_limits<int>::max())
      throw Exception(Error::InvalidArgument, "image size too large");

    const size_t pixelByteSize = getFormatSize(format);
    if (pixelByteStride != 0)
    {
      if (pixelByteStride < pixelByteSize)
        throw Exception(Error::InvalidArgument, "pixel stride smaller than pixel size");
      wByteStride = pixelByteStride;
    }
    else
      wByteStride = pixelByteSize;

    if (rowByteStride != 0)
    {
      if (rowByteStride < width * wByteStride)
        throw Exception(Error::InvalidArgument, "row stride smaller than width * pixel stride");
      hByteStride = rowByteStride;
    }
    else
      hByteStride = width * wByteStride;
  }

  Image::Image() :
    ImageDesc(Format::Undefined, 0, 0),
    ptr(nullptr) {}

  Image::Image(void* ptr, Format format, size_t width, size_t height, size_t byteOffset, size_t pixelByteStride, size_t rowByteStride)
    : ImageDesc(format, width, height, pixelByteStride, rowByteStride)
  {
    if ((ptr == nullptr) && (byteOffset + getByteSize() > 0))
      throw Exception(Error::InvalidArgument, "buffer region out of range");

    this->ptr = (char*)ptr + byteOffset;
  }

  Image::Image(const Ref<Buffer>& buffer, const ImageDesc& desc, size_t byteOffset)
    : Memory(buffer, byteOffset),
      ImageDesc(desc)
  {
    if (byteOffset + getByteSize() > buffer->getByteSize())
      throw Exception(Error::InvalidArgument, "buffer region out of range");

    this->ptr = buffer->getData() + byteOffset;
  }

  Image::Image(const Ref<Buffer>& buffer, Format format, size_t width, size_t height, size_t byteOffset, size_t pixelByteStride, size_t rowByteStride)
    : Memory(buffer, byteOffset),
      ImageDesc(format, width, height, pixelByteStride, rowByteStride)
  {
    if (byteOffset + getByteSize() > buffer->getByteSize())
      throw Exception(Error::InvalidArgument, "buffer region out of range");

    this->ptr = buffer->getData() + byteOffset;
  }

  Image::Image(const Ref<Engine>& engine, Format format, size_t width, size_t height)
    : Memory(engine->newBuffer(width * height * getFormatSize(format), Storage::Device)),
      ImageDesc(format, width, height)
  {
    this->ptr = buffer->getData();
  }

#if defined(OIDN_DEVICE_CPU)
  Image::operator ispc::ImageAccessor() const
  {
    ispc::ImageAccessor acc;
    acc.ptr = (uint8_t*)ptr;
    acc.hByteStride = hByteStride;
    acc.wByteStride = wByteStride;

    if (format != Format::Undefined)
    {
      switch (getDataType())
      {
      case DataType::Float32:
        acc.dataType = ispc::DataType_Float32;
        break;
      case DataType::Float16:
        acc.dataType = ispc::DataType_Float16;
        break;
      case DataType::UInt8:
        acc.dataType = ispc::DataType_UInt8;
        break;
      default:
        throw std::logic_error("unsupported data type");
      }
    }
    else
      acc.dataType = ispc::DataType_Float32;

    acc.W = int(width);
    acc.H = int(height);

    return acc;
  }
#endif

  void Image::updatePtr()
  {
    if (buffer)
    {
      if (byteOffset + getByteSize() > buffer->getByteSize())
        throw std::range_error("buffer region out of range");

      ptr = buffer->getData() + byteOffset;
    }
  }

  bool Image::overlaps(const Image& other) const
  {
    if (!ptr || !other.ptr)
      return false;

    // If the images are backed by different buffers, they cannot overlap
    if (buffer != other.buffer)
      return false;

    // Check whether the pointer intervals overlap
    const char* begin1 = begin();
    const char* end1   = end();
    const char* begin2 = other.begin();
    const char* end2   = other.end();

    return begin1 < end2 && begin2 < end1;
  }

} // namespace oidn