#ifndef _MAPBUFFER_H_
#define _MAPBUFFER_H_

#include <sys/mman.h> // mmap & munmap
#include <stdexcept> // run_time error
#include <libcamera/libcamera.h>

class MapBuffer {
private:
    void* data_;
    size_t length_;
public:
    // Constructor to map the plane into memory
    MapBuffer(const libcamera::FrameBuffer::Plane &plane)
        : data_(nullptr), length_(plane.length) {
        
        // Validate the frame
        if (!plane.fd.isValid() ||
            plane.offset == libcamera::FrameBuffer::Plane::kInvalidOffset) {
            throw std::runtime_error("Invalid FrameBuffer::Plane");
        }

        // Map the memory
        data_ = mmap(nullptr, length_, PROT_READ | PROT_WRITE, MAP_SHARED,
            plane.fd.get(), plane.offset);
        if (data_ == MAP_FAILED) {
            throw std::runtime_error("Failed to map buffer for plane");
        }
    }

    // Destructor: release resources
    ~MapBuffer() {
        if (data_ && data_ != MAP_FAILED && munmap(data_, length_) != 0) {
            std::cerr << "Error: Failed to unmap buffer." << std::endl;
        }
    }

    // Data-field assessors
    void* data() const {
        return static_cast<uint8_t*>(data_);
    }

    size_t length() const { 
        return length_;
    }

    // Non-copyable, non-movable
    MapBuffer(const MapBuffer &) = delete;
    MapBuffer &operator=(const MapBuffer &) = delete;
};

#endif