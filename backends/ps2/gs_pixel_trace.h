#pragma once
// Opt-in diagnostic: copy a bounded scanout ROI on the existing submission.
// The normal frame fence owns completion. Map and serialize only after drain.
#include "input_trace.h"
#include "device.hpp"
#include "command_buffer.hpp"
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace cave::ps2::pixel_trace {
inline std::atomic<bool> armed=false;
inline void Arm() noexcept { if(input_trace::Enabled())armed.store(true,std::memory_order_relaxed); }
class Capture {
    struct Row {uint64_t field;int64_t copied;};
    std::filesystem::path prefix;
    unsigned x{},y{},width{},height{},capacity{},fullWidth{},fullHeight{},format{};
    size_t stride{};
    std::vector<Row> rows;
    Vulkan::BufferHandle buffer;
    uint64_t submittedField{};
    uint64_t firstField{};
    bool saved=false,failed=false;
    void Fail(const char* message) {
        failed=true;std::cerr<<"[ps2-pixel-trace] "<<message<<'\n';
    }
public:
    Capture() {
        if(!input_trace::Enabled())return;
        const auto* path=_wgetenv(L"EVAC_PS2_PIXEL_TRACE");
        if(!path || !*path)return;
        const auto* region=_wgetenv(L"EVAC_PS2_PIXEL_REGION");
        wchar_t extra{};
        if(!region || std::swscanf(region,L"%u,%u,%u,%u,%u%c",&x,&y,&width,&height,&capacity,&extra)!=5 ||
           (bool(width)!=bool(height)) || (!width && (x || y)) ||
           width>2048 || height>2048 || !capacity || capacity>2048 ||
           uint64_t(width)*height*capacity>32u*1024u*1024u)
            throw std::runtime_error("Invalid PS2 diagnostic pixel region");
        prefix=std::filesystem::absolute(path);
        if(const auto* start=_wgetenv(L"EVAC_PS2_PIXEL_START_FIELD");start && *start) {
            wchar_t* end{};
            firstField=std::wcstoull(start,&end,10);
            if(*end || !firstField || *start==L'-')
                throw std::runtime_error("Invalid PS2 diagnostic pixel start field");
        }
        if(std::filesystem::exists(prefix.wstring()+L".bin") || std::filesystem::exists(prefix.wstring()+L".csv"))
            throw std::runtime_error("PS2 pixel output already exists");
        stride=size_t(width)*height*4u;
        rows.reserve(capacity);armed.store(false,std::memory_order_relaxed);
    }
    void Copy(Vulkan::Device& device,Vulkan::CommandBuffer& command,const Vulkan::Image& source,
              uint64_t field,bool transferSource) {
        if(prefix.empty() || failed || rows.size()==capacity)return;
        if(firstField ? field<firstField : !armed.load(std::memory_order_relaxed))return;
        if(!buffer) {
            fullWidth=source.get_width();fullHeight=source.get_height();format=source.get_create_info().format;
            if(!width){width=fullWidth;height=fullHeight;stride=size_t(width)*height*4u;}
        }
        if(!transferSource || (source.get_create_info().format!=VK_FORMAT_R8G8B8A8_UNORM &&
                              source.get_create_info().format!=VK_FORMAT_B8G8R8A8_UNORM) ||
           uint64_t(x)+width>source.get_width() || uint64_t(y)+height>source.get_height() ||
           uint64_t(width)*height*capacity>32u*1024u*1024u) {
            Fail("Unsupported PS2 pixel region or scanout format; capture disabled");return;
        }
        if(!buffer) {
            Vulkan::BufferCreateInfo info{};
            info.domain=Vulkan::BufferDomain::CachedHost;info.size=stride*capacity;info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            buffer=device.create_buffer(info);
            if(!buffer){Fail("Cannot allocate PS2 pixel trace buffer");return;}
        }
        if(fullWidth!=source.get_width() || fullHeight!=source.get_height() || format!=source.get_create_info().format) {
            Fail("PS2 scanout changed during pixel capture; capture disabled");return;
        }
        const VkBufferImageCopy copy{VkDeviceSize(rows.size()*stride),0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
                                     {int32_t(x),int32_t(y),0},{width,height,1}};
        command.copy_image_to_buffer(*buffer,source,1,&copy);
        command.buffer_barrier(*buffer,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_HOST_BIT,VK_ACCESS_2_HOST_READ_BIT);
        rows.push_back({field,input_trace::Stamp()});
    }
    void Submitted(uint64_t field) noexcept {submittedField=field;}
    // Caller has waited for the final existing frame fence before this call.
    void Save(Vulkan::Device& device) {
        if(prefix.empty() || saved)return;
        if(!rows.empty() && rows.back().field>submittedField)
            throw std::runtime_error("PS2 pixel trace contains an unsubmitted frame");
        const auto binary=prefix.wstring()+L".bin",metadata=prefix.wstring()+L".csv";
        if(std::filesystem::exists(binary) || std::filesystem::exists(metadata))
            throw std::runtime_error("PS2 pixel output already exists");
        std::ofstream bytes(std::filesystem::path(binary),std::ios::binary);
        std::ofstream table{std::filesystem::path(metadata)};
        if(!bytes || !table)throw std::runtime_error("Cannot open PS2 pixel trace output");
        if(!rows.empty()) {
            const auto* mapped=device.map_host_buffer(*buffer,Vulkan::MEMORY_ACCESS_READ_BIT);
            if(!mapped)throw std::runtime_error("Cannot map PS2 diagnostic readback");
            bytes.write(static_cast<const char*>(mapped),std::streamsize(rows.size()*stride));
            device.unmap_host_buffer(*buffer,Vulkan::MEMORY_ACCESS_READ_BIT);
        }
        table<<"# version=1 full_width="<<fullWidth<<" full_height="<<fullHeight<<" x="<<x<<" y="<<y
             <<" width="<<width<<" height="<<height<<" format="<<format<<" stride="<<stride
             <<" capacity="<<capacity<<" captured="<<rows.size()<<" failed="<<failed<<'\n'<<"index,field,copy_qpc_us\n";
        for(size_t i=0;i<rows.size();++i)table<<i<<','<<rows[i].field<<','<<rows[i].copied<<'\n';
        bytes.flush();table.flush();
        if(!bytes || !table)throw std::runtime_error("Cannot write PS2 pixel trace");
        saved=true;buffer.reset();
    }
};
}
