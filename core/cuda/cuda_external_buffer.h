// Copyright 2009-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../buffer.h"
#include "cuda_engine.h"

namespace oidn {

  class CUDAExternalBuffer : public USMBuffer
  {
  public:
    CUDAExternalBuffer(const Ref<Engine>& engine,
                       ExternalMemoryTypeFlag fdType,
                       int fd, size_t byteSize);

    CUDAExternalBuffer(const Ref<Engine>& engine,
                       ExternalMemoryTypeFlag handleType,
                       void* handle, const void* name, size_t byteSize);

    ~CUDAExternalBuffer();

  private:
    cudaExternalMemory_t extMem;

    void init(const cudaExternalMemoryHandleDesc& handleDesc);
  };

} // namespace oidn
