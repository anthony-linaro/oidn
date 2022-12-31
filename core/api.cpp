// Copyright 2009-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#define OIDN_EXPORT_API

// Locks the device that owns the specified object
// Use *only* inside OIDN_TRY/CATCH!
#define OIDN_LOCK(obj) \
  std::lock_guard<std::mutex> lock(obj->getDevice()->getMutex());

// Try/catch for converting exceptions to errors
#define OIDN_TRY \
  try {

#if defined(OIDN_DNNL)
  #define OIDN_CATCH_DNNL(obj) \
    } catch (dnnl::error& e) {                                                                        \
      if (e.status == dnnl_out_of_memory)                                                             \
        Device::setError(obj ? obj->getDevice() : nullptr, Error::OutOfMemory, "out of memory");      \
      else                                                                                            \
        Device::setError(obj ? obj->getDevice() : nullptr, Error::Unknown, e.message);
#else
  #define OIDN_CATCH_DNNL(obj)
#endif

#define OIDN_CATCH(obj) \
  } catch (Exception& e) {                                                                          \
    Device::setError(obj ? obj->getDevice() : nullptr, e.code(), e.what());                         \
  } catch (std::bad_alloc&) {                                                                       \
    Device::setError(obj ? obj->getDevice() : nullptr, Error::OutOfMemory, "out of memory");        \
  OIDN_CATCH_DNNL(obj)                                                                              \
  } catch (std::exception& e) {                                                                     \
    Device::setError(obj ? obj->getDevice() : nullptr, Error::Unknown, e.what());                   \
  } catch (...) {                                                                                   \
    Device::setError(obj ? obj->getDevice() : nullptr, Error::Unknown, "unknown exception caught"); \
  }

#include "common/platform.h"
#if defined(OIDN_DEVICE_CPU)
  #include "cpu/cpu_device.h"
#endif
#if defined(OIDN_DEVICE_SYCL)
  #include "sycl/sycl_device.h"
#endif
#if defined(OIDN_DEVICE_CUDA)
  #include "cuda/cuda_device.h"
#endif
#if defined(OIDN_DEVICE_HIP)
  #include "hip/hip_device.h"
#endif
#include "filter.h"
#include <mutex>

using namespace oidn;

OIDN_API_NAMESPACE_BEGIN

  namespace
  {
    OIDN_INLINE void checkHandle(void* handle)
    {
      if (handle == nullptr)
        throw Exception(Error::InvalidArgument, "invalid handle");
    }

    template<typename T>
    OIDN_INLINE void retainObject(T* obj)
    {
      if (obj)
      {
        obj->incRef();
      }
      else
      {
        OIDN_TRY
          checkHandle(obj);
        OIDN_CATCH(obj)
      }
    }

    template<typename T>
    OIDN_INLINE void releaseObject(T* obj)
    {
      if (obj == nullptr || obj->decRefKeep() == 0)
      {
        OIDN_TRY
          checkHandle(obj);
          OIDN_LOCK(obj);
          obj->getDevice()->wait(); // wait for all async operations to complete
          obj->destroy();
        OIDN_CATCH(obj)
      }
    }

    template<>
    OIDN_INLINE void releaseObject(Device* obj)
    {
      if (obj == nullptr || obj->decRefKeep() == 0)
      {
        OIDN_TRY
          checkHandle(obj);
          // Do NOT lock the device because it owns the mutex
          obj->wait(); // wait for all async operations to complete
          obj->destroy();
        OIDN_CATCH(obj)
      }
    }
  }

  OIDN_API OIDNDevice oidnNewDevice(OIDNDeviceType type)
  {
    Ref<Device> device = nullptr;
    OIDN_TRY
    #if defined(OIDN_DEVICE_CUDA)
      if (type == OIDN_DEVICE_TYPE_CUDA || (type == OIDN_DEVICE_TYPE_DEFAULT && CUDADevice::isSupported()))
        device = makeRef<CUDADevice>();
      else
    #endif
    #if defined(OIDN_DEVICE_HIP)
      if (type == OIDN_DEVICE_TYPE_HIP || (type == OIDN_DEVICE_TYPE_DEFAULT && HIPDevice::isSupported()))
        device = makeRef<HIPDevice>();
      else
    #endif
    #if defined(OIDN_DEVICE_SYCL)
      if (type == OIDN_DEVICE_TYPE_SYCL || (type == OIDN_DEVICE_TYPE_DEFAULT && SYCLDevice::isSupported()))
        device = makeRef<SYCLDevice>();
      else
    #endif
    #if defined(OIDN_DEVICE_CPU)
      if (type == OIDN_DEVICE_TYPE_CPU || type == OIDN_DEVICE_TYPE_DEFAULT)
        device = makeRef<CPUDevice>();
      else
    #endif
        throw Exception(Error::InvalidArgument, "unsupported device type");
    OIDN_CATCH(device)
    return (OIDNDevice)device.detach();
  }

#if defined(OIDN_DEVICE_SYCL)
  OIDN_API OIDNDevice oidnNewSYCLDevice(const sycl::queue* queues, int numQueues)
  {
    Ref<Device> device = nullptr;
    OIDN_TRY
      if (numQueues < 0)
        throw Exception(Error::InvalidArgument, "invalid number of queues");
      device = makeRef<SYCLDevice>(std::vector<sycl::queue>{queues, queues + numQueues});
    OIDN_CATCH(device)
    return (OIDNDevice)device.detach();
  }
#endif

#if defined(OIDN_DEVICE_CUDA)
  OIDN_API OIDNDevice oidnNewCUDADevice(const cudaStream_t* streams, int numStreams)
  {
    Ref<Device> device = nullptr;
    OIDN_TRY
      if (numStreams == 1)
        device = makeRef<CUDADevice>(streams[0]);
      else if (numStreams == 0)
        device = makeRef<CUDADevice>();
      else
        throw Exception(Error::InvalidArgument, "unsupported number of streams");
    OIDN_CATCH(device)
    return (OIDNDevice)device.detach();
  }
#endif

#if defined(OIDN_DEVICE_HIP)
  OIDN_API OIDNDevice oidnNewHIPDevice(const hipStream_t* streams, int numStreams)
  {
    Ref<Device> device = nullptr;
    OIDN_TRY
      if (numStreams == 1)
        device = makeRef<HIPDevice>(streams[0]);
      else if (numStreams == 0)
        device = makeRef<HIPDevice>();
      else
        throw Exception(Error::InvalidArgument, "unsupported number of streams");
    OIDN_CATCH(device)
    return (OIDNDevice)device.detach();
  }
#endif

  OIDN_API void oidnRetainDevice(OIDNDevice hDevice)
  {
    Device* device = (Device*)hDevice;
    retainObject(device);
  }

  OIDN_API void oidnReleaseDevice(OIDNDevice hDevice)
  {
    Device* device = (Device*)hDevice;
    releaseObject(device);
  }

  OIDN_API void oidnSetDevice1b(OIDNDevice hDevice, const char* name, bool value)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->set1i(name, value);
    OIDN_CATCH(device)
  }

  OIDN_API void oidnSetDevice1i(OIDNDevice hDevice, const char* name, int value)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->set1i(name, value);
    OIDN_CATCH(device)
  }

  OIDN_API bool oidnGetDevice1b(OIDNDevice hDevice, const char* name)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      return device->get1i(name);
    OIDN_CATCH(device)
    return false;
  }

  OIDN_API int oidnGetDevice1i(OIDNDevice hDevice, const char* name)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      return device->get1i(name);
    OIDN_CATCH(device)
    return 0;
  }

  OIDN_API void oidnSetDeviceErrorFunction(OIDNDevice hDevice, OIDNErrorFunction func, void* userPtr)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->setErrorFunction((ErrorFunction)func, userPtr);
    OIDN_CATCH(device)
  }

  OIDN_API OIDNError oidnGetDeviceError(OIDNDevice hDevice, const char** outMessage)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      return (OIDNError)Device::getError(device, outMessage);
    OIDN_CATCH(device)
    if (outMessage) *outMessage = "";
    return OIDN_ERROR_UNKNOWN;
  }

  OIDN_API void oidnCommitDevice(OIDNDevice hDevice)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->commit();
    OIDN_CATCH(device)
  }

  OIDN_API void oidnSyncDevice(OIDNDevice hDevice)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->wait();
    OIDN_CATCH(device)
  }

  OIDN_API OIDNBuffer oidnNewBuffer(OIDNDevice hDevice, size_t byteSize)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->checkCommitted();
      Ref<Buffer> buffer = device->getEngine()->newBuffer(byteSize, Storage::Host);
      return (OIDNBuffer)buffer.detach();
    OIDN_CATCH(device)
    return nullptr;
  }

  OIDN_API OIDNBuffer oidnNewBufferWithStorage(OIDNDevice hDevice, size_t byteSize, OIDNStorage storage)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->checkCommitted();
      Ref<Buffer> buffer = device->getEngine()->newBuffer(byteSize, (Storage)storage);
      return (OIDNBuffer)buffer.detach();
    OIDN_CATCH(device)
    return nullptr;
  }

  OIDN_API OIDNBuffer oidnNewSharedBuffer(OIDNDevice hDevice, void* devPtr, size_t byteSize)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->checkCommitted();
      Ref<Buffer> buffer = device->getEngine()->newBuffer(devPtr, byteSize);
      return (OIDNBuffer)buffer.detach();
    OIDN_CATCH(device)
    return nullptr;
  }

  OIDN_API OIDNBuffer oidnNewSharedBufferFromFD(OIDNDevice hDevice,
                                                OIDNExternalMemoryTypeFlag fdType,
                                                int fd, size_t byteSize)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->checkCommitted();
      if (!((ExternalMemoryTypeFlag)fdType & device->getExternalMemoryTypes()))
        throw Exception(Error::InvalidArgument, "external memory type not supported by the device");
      Ref<Buffer> buffer = device->getEngine()->newExternalBuffer(
        (ExternalMemoryTypeFlag)fdType, fd, byteSize);
      return (OIDNBuffer)buffer.detach();
    OIDN_CATCH(device)
    return nullptr;
  }

  OIDN_API OIDNBuffer oidnNewSharedBufferFromWin32Handle(OIDNDevice hDevice,
                                                         OIDNExternalMemoryTypeFlag handleType,
                                                         void* handle, const void* name, size_t byteSize)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->checkCommitted();
      if (!((ExternalMemoryTypeFlag)handleType & device->getExternalMemoryTypes()))
        throw Exception(Error::InvalidArgument, "external memory type not supported by the device");
      if ((!handle && !name) || (handle && name))
        throw Exception(Error::InvalidArgument, "exactly one of the external memory handle and name must be non-null");
      Ref<Buffer> buffer = device->getEngine()->newExternalBuffer(
        (ExternalMemoryTypeFlag)handleType, handle, name, byteSize);
      return (OIDNBuffer)buffer.detach();
    OIDN_CATCH(device)
    return nullptr;
  }

  OIDN_API void oidnRetainBuffer(OIDNBuffer hBuffer)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    retainObject(buffer);
  }

  OIDN_API void oidnReleaseBuffer(OIDNBuffer hBuffer)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    releaseObject(buffer);
  }

  OIDN_API void* oidnMapBuffer(OIDNBuffer hBuffer, OIDNAccess access, size_t byteOffset, size_t byteSize)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    OIDN_TRY
      checkHandle(hBuffer);
      OIDN_LOCK(buffer);
      return buffer->map(byteOffset, byteSize, (Access)access);
    OIDN_CATCH(buffer)
    return nullptr;
  }

  OIDN_API void oidnUnmapBuffer(OIDNBuffer hBuffer, void* mappedPtr)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    OIDN_TRY
      checkHandle(hBuffer);
      OIDN_LOCK(buffer);
      return buffer->unmap(mappedPtr);
    OIDN_CATCH(buffer)
  }

  OIDN_API void oidnReadBuffer(OIDNBuffer hBuffer, size_t byteOffset, size_t byteSize, void* dstHostPtr)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    OIDN_TRY
      checkHandle(hBuffer);
      OIDN_LOCK(buffer);
      buffer->read(byteOffset, byteSize, dstHostPtr);
    OIDN_CATCH(buffer);
  }

  OIDN_API void oidnReadBufferAsync(OIDNBuffer hBuffer, size_t byteOffset, size_t byteSize, void* dstHostPtr)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    OIDN_TRY
      checkHandle(hBuffer);
      OIDN_LOCK(buffer);
      buffer->read(byteOffset, byteSize, dstHostPtr, SyncMode::Async);
    OIDN_CATCH(buffer);
  }

  OIDN_API void oidnWriteBuffer(OIDNBuffer hBuffer, size_t byteOffset, size_t byteSize, const void* srcHostPtr)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    OIDN_TRY
      checkHandle(hBuffer);
      OIDN_LOCK(buffer);
      buffer->write(byteOffset, byteSize, srcHostPtr);
    OIDN_CATCH(buffer);
  }

  OIDN_API void oidnWriteBufferAsync(OIDNBuffer hBuffer, size_t byteOffset, size_t byteSize, const void* srcHostPtr)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    OIDN_TRY
      checkHandle(hBuffer);
      OIDN_LOCK(buffer);
      buffer->write(byteOffset, byteSize, srcHostPtr, SyncMode::Async);
    OIDN_CATCH(buffer);
  }

  OIDN_API void* oidnGetBufferData(OIDNBuffer hBuffer)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    OIDN_TRY
      checkHandle(hBuffer);
      OIDN_LOCK(buffer);
      return buffer->getData();
    OIDN_CATCH(buffer)
    return nullptr;
  }

  OIDN_API size_t oidnGetBufferSize(OIDNBuffer hBuffer)
  {
    Buffer* buffer = (Buffer*)hBuffer;
    OIDN_TRY
      checkHandle(hBuffer);
      OIDN_LOCK(buffer);
      return buffer->getByteSize();
    OIDN_CATCH(buffer)
    return 0;
  }

  OIDN_API OIDNFilter oidnNewFilter(OIDNDevice hDevice, const char* type)
  {
    Device* device = (Device*)hDevice;
    OIDN_TRY
      checkHandle(hDevice);
      OIDN_LOCK(device);
      device->checkCommitted();
      Ref<Filter> filter = device->newFilter(type);
      return (OIDNFilter)filter.detach();
    OIDN_CATCH(device)
    return nullptr;
  }

  OIDN_API void oidnRetainFilter(OIDNFilter hFilter)
  {
    Filter* filter = (Filter*)hFilter;
    retainObject(filter);
  }

  OIDN_API void oidnReleaseFilter(OIDNFilter hFilter)
  {
    Filter* filter = (Filter*)hFilter;
    releaseObject(filter);
  }

  OIDN_API void oidnSetFilterImage(OIDNFilter hFilter, const char* name,
                                   OIDNBuffer hBuffer, OIDNFormat format,
                                   size_t width, size_t height,
                                   size_t byteOffset,
                                   size_t pixelByteStride, size_t rowByteStride)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      checkHandle(hBuffer);
      OIDN_LOCK(filter);
      Ref<Buffer> buffer = (Buffer*)hBuffer;
      if (buffer->getDevice() != filter->getDevice())
        throw Exception(Error::InvalidArgument, "the specified objects are bound to different devices");
      auto image = std::make_shared<Image>(buffer, (Format)format, (int)width, (int)height, byteOffset, pixelByteStride, rowByteStride);
      filter->setImage(name, image);
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnSetSharedFilterImage(OIDNFilter hFilter, const char* name,
                                         void* devPtr, OIDNFormat format,
                                         size_t width, size_t height,
                                         size_t byteOffset,
                                         size_t pixelByteStride, size_t rowByteStride)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      auto image = std::make_shared<Image>(devPtr, (Format)format, (int)width, (int)height, byteOffset, pixelByteStride, rowByteStride);
      filter->setImage(name, image);
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnRemoveFilterImage(OIDNFilter hFilter, const char* name)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->removeImage(name);
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnSetSharedFilterData(OIDNFilter hFilter, const char* name,
                                        void* hostPtr, size_t byteSize)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      Data data(hostPtr, byteSize);
      filter->setData(name, data);
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnUpdateFilterData(OIDNFilter hFilter, const char* name)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->updateData(name);
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnRemoveFilterData(OIDNFilter hFilter, const char* name)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->removeData(name);
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnSetFilter1b(OIDNFilter hFilter, const char* name, bool value)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->set1i(name, int(value));
    OIDN_CATCH(filter)
  }

  OIDN_API bool oidnGetFilter1b(OIDNFilter hFilter, const char* name)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      return filter->get1i(name);
    OIDN_CATCH(filter)
    return false;
  }

  OIDN_API void oidnSetFilter1i(OIDNFilter hFilter, const char* name, int value)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->set1i(name, value);
    OIDN_CATCH(filter)
  }

  OIDN_API int oidnGetFilter1i(OIDNFilter hFilter, const char* name)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      return filter->get1i(name);
    OIDN_CATCH(filter)
    return 0;
  }

  OIDN_API void oidnSetFilter1f(OIDNFilter hFilter, const char* name, float value)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->set1f(name, value);
    OIDN_CATCH(filter)
  }

  OIDN_API float oidnGetFilter1f(OIDNFilter hFilter, const char* name)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      return filter->get1f(name);
    OIDN_CATCH(filter)
    return 0;
  }

  OIDN_API void oidnSetFilterProgressMonitorFunction(OIDNFilter hFilter, OIDNProgressMonitorFunction func, void* userPtr)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->setProgressMonitorFunction(func, userPtr);
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnCommitFilter(OIDNFilter hFilter)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->commit();
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnExecuteFilter(OIDNFilter hFilter)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->execute();
    OIDN_CATCH(filter)
  }

  OIDN_API void oidnExecuteFilterAsync(OIDNFilter hFilter)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      checkHandle(hFilter);
      OIDN_LOCK(filter);
      filter->execute(SyncMode::Async);
    OIDN_CATCH(filter)
  }

#if defined(OIDN_DEVICE_SYCL)
  OIDN_API void oidnExecuteSYCLFilterAsync(OIDNFilter hFilter,
                                           const sycl::event* depEvents,
                                           int numDepEvents,
                                           sycl::event* doneEvent)
  {
    Filter* filter = (Filter*)hFilter;
    OIDN_TRY
      // Check the parameters
      checkHandle(hFilter);
      if (numDepEvents < 0)
        throw Exception(Error::InvalidArgument, "invalid number of dependent events");

      OIDN_LOCK(filter);

      // Check whether the filter belongs to a SYCL device
      SYCLDevice* device = dynamic_cast<SYCLDevice*>(filter->getDevice());
      if (device == nullptr)
        throw Exception(Error::InvalidArgument, "filter does not belong to a SYCL device");

      // Execute the filter
      device->setDepEvents({depEvents, depEvents + numDepEvents});
      filter->execute(SyncMode::Async);
      auto doneEvents = device->getDoneEvents();

      // Output the completion event (optional)
      if (doneEvent != nullptr)
      {
        if (doneEvents.size() == 1)
          *doneEvent = doneEvents[0];
        else if (doneEvents.size() == 0)
          *doneEvent = {}; // no kernels were executed
        else
          throw std::logic_error("missing barrier after filter kernels");
      }
    OIDN_CATCH(filter)
  }
#endif

OIDN_API_NAMESPACE_END
