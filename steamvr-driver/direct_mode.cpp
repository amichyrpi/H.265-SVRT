#include "direct_mode.h"
#include <dxgi1_2.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;
static void debug(const char *s){char line[2304];std::snprintf(line,sizeof(line),"SVRT: %s",s);OutputDebugStringA(line);OutputDebugStringA("\n");if(vr::VRDriverLog())vr::VRDriverLog()->Log(line);}
static void debugf(const char *format,...){char message[2048];va_list args;va_start(args,format);std::vsnprintf(message,sizeof(message),format,args);va_end(args);debug(message);}
SvrtDirectMode::SvrtDirectMode()=default;
SvrtDirectMode::~SvrtDirectMode(){Stop();}
bool SvrtDirectMode::Start(const std::string &host,uint16_t port,unsigned fps,unsigned bitrate,const std::string &ffmpeg,const std::string &encoder){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if (running_) return true;
  host_=host;port_=port;fps_=fps?fps:60;bitrate_=bitrate?bitrate:35;ffmpeg_=ffmpeg.empty()?"ffmpeg.exe":ffmpeg;encoder_=encoder.empty()?"hevc_nvenc":encoder;
  encoder_failed_=false; accepting_=false;UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;D3D_FEATURE_LEVEL level;HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device_,&level,&context_);if(FAILED(hr)){debugf("D3D11CreateDevice failed: 0x%08lx",(unsigned long)hr);encoder_failed_=true;return false;}debugf("direct mode ready: receiver=%s:%u fps=%u bitrate=%uM encoder=%s",host_.c_str(),port_,fps_,bitrate_,encoder_.c_str());running_=true;accepting_=true;worker_=std::thread(&SvrtDirectMode::EncoderThread,this);return true;
}
void SvrtDirectMode::SetReceiverAvailable(bool available){
  const bool was_available=receiver_available_.exchange(available);
  if(available&&!was_available)encoder_failed_=false;
  if(!available){
    latest_virtual_handle_=0;
    // Cancel a pipe write immediately when the receiver disappears. Without
    // this, vrserver can wait forever for FFmpeg and make the desktop appear
    // frozen during disconnect or SteamVR shutdown.
    if(worker_.joinable()) CancelSynchronousIo(worker_.native_handle());
  }
  ready_.notify_one();
}
void SvrtDirectMode::Stop(){
  std::unique_lock<std::mutex> lifecycle(lifecycle_mutex_);
  accepting_=false;
  if(!running_.exchange(false)){return;}
  // A synchronous WriteFile to FFmpeg's stdin can block indefinitely when the
  // receiver disappears.  Stop the reader first: this breaks that write and
  // lets the SteamVR shutdown thread join the worker promptly.
  if(worker_.joinable()) CancelSynchronousIo(worker_.native_handle());
  if(process_) TerminateProcess(process_, 0);
  ready_.notify_all();
  if(worker_.joinable())worker_.join();
  CloseEncoder();
  std::lock_guard<std::mutex> l(mutex_);textures_.clear();virtual_textures_.clear();slots_.clear();frame_.clear();context_.Reset();device_.Reset();
}
void SvrtDirectMode::CreateSwapTextureSet(uint32_t pid,const SwapTextureSetDesc_t *d,SwapTextureSet_t *out){
  *out={};if(!device_||!d)return;debugf("creating swap textures: pid=%u size=%ux%u format=%u samples=%u",pid,d->nWidth,d->nHeight,d->nFormat,d->nSampleCount);D3D11_TEXTURE2D_DESC td{};td.Width=d->nWidth;td.Height=d->nHeight;td.MipLevels=1;td.ArraySize=1;td.Format=(DXGI_FORMAT)d->nFormat;td.SampleDesc.Count=std::max(1u,d->nSampleCount);td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
  std::lock_guard<std::mutex> l(mutex_);Texture created[3];uint64_t keys[3]{};for(int i=0;i<3;i++){ComPtr<ID3D11Texture2D> tex;if(FAILED(device_->CreateTexture2D(&td,nullptr,&tex)))return;ComPtr<IDXGIResource> dxgi;if(FAILED(tex.As(&dxgi)))return;HANDLE h=nullptr;if(FAILED(dxgi->GetSharedHandle(&h))||!h)return;keys[i]=(uint64_t)(uintptr_t)h;created[i]=Texture{pid,keys[0],tex};}for(int i=0;i<3;i++){created[i].group=keys[0];textures_[keys[i]]=std::move(created[i]);out->rSharedTextureHandles[i]=(vr::SharedTextureHandle_t)keys[i];}out->unTextureFlags=0;
}
void SvrtDirectMode::DestroySwapTextureSet(vr::SharedTextureHandle_t h){std::lock_guard<std::mutex> l(mutex_);auto it=textures_.find((uint64_t)h);if(it==textures_.end())return;uint64_t group=it->second.group;for(auto p=textures_.begin();p!=textures_.end();){if(p->second.group==group)p=textures_.erase(p);else ++p;}}
void SvrtDirectMode::DestroyAllSwapTextureSets(uint32_t pid){std::lock_guard<std::mutex> l(mutex_);for(auto p=textures_.begin();p!=textures_.end();){if(p->second.pid==pid)p=textures_.erase(p);else ++p;}}
void SvrtDirectMode::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t[2],uint32_t (*indices)[2]){next_[0]=(next_[0]+1)%3;next_[1]=(next_[1]+1)%3;(*indices)[0]=next_[0];(*indices)[1]=next_[1];}
void SvrtDirectMode::SubmitLayer(const SubmitLayerPerEye_t (&eyes)[2]){std::lock_guard<std::mutex> l(mutex_);submitted_[0]=eyes[0].hTexture;submitted_[1]=eyes[1].hTexture;}
bool SvrtDirectMode::EnsureSlots(unsigned ew,unsigned h,DXGI_FORMAT fmt){if(!slots_.empty())return true;width_=std::min(ew,1920u)*2;height_=std::min(h,2160u);pixel_format_=(fmt==DXGI_FORMAT_R8G8B8A8_UNORM||fmt==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)?"rgba":"bgra";debugf("preparing fixed stream: source=%ux%u output=%ux%u pixel_format=%s",ew,h,width_,height_,pixel_format_.c_str());D3D11_TEXTURE2D_DESC d{};d.Width=width_;d.Height=height_;d.MipLevels=d.ArraySize=1;d.Format=fmt;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Slot s;if(FAILED(device_->CreateTexture2D(&d,nullptr,&s.staging))){debug("failed to create CPU staging texture");return false;}slots_.push_back(std::move(s));frame_.resize((size_t)width_*height_*4);return StartEncoder(width_,height_);}
bool SvrtDirectMode::EnsureVirtualSlots(unsigned w,unsigned h,DXGI_FORMAT fmt){if(!slots_.empty())return true;width_=std::min(w,4096u);height_=std::min(h,2160u);pixel_format_=(fmt==DXGI_FORMAT_R8G8B8A8_UNORM||fmt==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)?"rgba":"bgra";debugf("preparing virtual-display stream: %ux%u pixel_format=%s",width_,height_,pixel_format_.c_str());D3D11_TEXTURE2D_DESC d{};d.Width=width_;d.Height=height_;d.MipLevels=d.ArraySize=1;d.Format=fmt;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Slot s;if(FAILED(device_->CreateTexture2D(&d,nullptr,&s.staging))){debug("failed to create CPU staging texture");return false;}slots_.push_back(std::move(s));frame_.resize((size_t)width_*height_*4);return StartEncoder(width_,height_);}
void SvrtDirectMode::Present(vr::SharedTextureHandle_t){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!accepting_) return;
  std::unique_lock<std::mutex> l(mutex_);auto a=textures_.find((uint64_t)submitted_[0]),b=textures_.find((uint64_t)submitted_[1]);if(a==textures_.end()||b==textures_.end())return;D3D11_TEXTURE2D_DESC d{};a->second.texture->GetDesc(&d);std::lock_guard<std::mutex> dl(d3d_mutex_);if(!EnsureSlots(d.Width,d.Height,d.Format))return;auto slot=std::find_if(slots_.begin(),slots_.end(),[](const Slot&s){return !s.pending;});if(slot==slots_.end())return;unsigned eye_width=width_/2,copy_width=std::min(d.Width,eye_width),copy_height=std::min(d.Height,height_),source_x=(d.Width-copy_width)/2,source_y=(d.Height-copy_height)/2;D3D11_BOX box{source_x,source_y,0,source_x+copy_width,source_y+copy_height,1};context_->CopySubresourceRegion(slot->staging.Get(),0,0,0,0,a->second.texture.Get(),0,&box);context_->CopySubresourceRegion(slot->staging.Get(),0,eye_width,0,0,b->second.texture.Get(),0,&box);context_->Flush();slot->pending=true;slot->sequence=++sequence_;l.unlock();ready_.notify_one();}
void SvrtDirectMode::PresentVirtual(vr::SharedTextureHandle_t handle){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!accepting_||!device_||!handle||!receiver_available_||encoder_failed_)return;

  // The handle is only guaranteed to refer to the compositor backbuffer for
  // the duration of Present().  Deferring OpenSharedResource/CopyResource to
  // the encoder thread races SteamVR reusing that texture and produces a
  // perfectly valid stream of stale (usually grey) pixels.  Copy the GPU
  // resource while the compositor owns it, then let the worker perform the
  // asynchronous map/encode.
  if(CopyVirtualFrame(handle)) ready_.notify_one();
}
bool SvrtDirectMode::CopyVirtualFrame(vr::SharedTextureHandle_t handle){
  ComPtr<ID3D11Texture2D> source;
  {
    std::lock_guard<std::mutex> l(mutex_);
    auto cached=virtual_textures_.find(static_cast<uintptr_t>(handle));
    if(cached!=virtual_textures_.end()) source=cached->second;
  }
  if(!source && SUCCEEDED(device_->OpenSharedResource(
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle)),
      IID_PPV_ARGS(&source)))) {
    std::lock_guard<std::mutex> l(mutex_);
    if(virtual_textures_.size()>=4) virtual_textures_.clear();
    virtual_textures_[static_cast<uintptr_t>(handle)]=source;
  }
  if(!source){
    debugf("virtual display: OpenSharedResource failed for handle=%p",
           reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
    return false;
  }

  ComPtr<IDXGIKeyedMutex> keyed;
  const bool has_keyed_mutex=SUCCEEDED(source.As(&keyed));
  if(has_keyed_mutex){
    const HRESULT acquired=keyed->AcquireSync(0,10);
    if(acquired!=S_OK){
      debugf("virtual display: AcquireSync failed: 0x%08lx",
             static_cast<unsigned long>(acquired));
      return false;
    }
  }

  bool copied=false;
  {
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    if(d.Width!=logged_virtual_width_||d.Height!=logged_virtual_height_||
       d.Format!=logged_virtual_format_){
      debugf("virtual display source: %ux%u format=%u samples=%u array=%u mips=%u misc=0x%08x",
             d.Width,d.Height,static_cast<unsigned>(d.Format),d.SampleDesc.Count,
             d.ArraySize,d.MipLevels,d.MiscFlags);
      logged_virtual_width_=d.Width;
      logged_virtual_height_=d.Height;
      logged_virtual_format_=d.Format;
    }
    if(d.SampleDesc.Count==1 && EnsureVirtualSlots(d.Width,d.Height,d.Format)){
      std::lock_guard<std::mutex> l(mutex_);
      auto slot=std::find_if(slots_.begin(),slots_.end(),
                             [](const Slot&s){return !s.pending;});
      if(slot!=slots_.end()){
        // CopyResource has no HRESULT; a device-removal check catches the
        // only asynchronous failure that can invalidate the copy.
        context_->CopyResource(slot->staging.Get(),source.Get());
        context_->Flush();
        const HRESULT device_status=device_->GetDeviceRemovedReason();
        if(SUCCEEDED(device_status)){
          slot->pending=true;
          slot->sequence=++sequence_;
          copied=true;
        }else{
          debugf("virtual display: CopyResource device error: 0x%08lx",
                 static_cast<unsigned long>(device_status));
        }
      }
    }else{
      debug("virtual display: unsupported source texture description");
    }
  }
  if(has_keyed_mutex) keyed->ReleaseSync(0);
  return copied;
}
bool SvrtDirectMode::StartEncoder(unsigned w,unsigned h){if(pipe_!=INVALID_HANDLE_VALUE)return true;if(w>4096){debugf("refusing %ux%u stream: Raspberry Pi 4 HEVC width limit is 4096",w,h);encoder_failed_=true;return false;}SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read_pipe=nullptr;if(!CreatePipe(&read_pipe,&pipe_,&sa,1<<20)){encoder_failed_=true;debugf("CreatePipe failed: %lu",GetLastError());return false;}SetHandleInformation(pipe_,HANDLE_FLAG_INHERIT,0);char cmd[2048];std::snprintf(cmd,sizeof(cmd),"\"%s\" -hide_banner -loglevel warning -fflags nobuffer -f rawvideo -pix_fmt %s -video_size %ux%u -framerate %u -i pipe:0 -an -c:v %s -preset p1 -tune ull -rc cbr -b:v %uM -maxrate %uM -bufsize %uM -g %u -bf 0 -muxdelay 0 -f mpegts \"tcp://%s:%u?tcp_nodelay=1\"",ffmpeg_.c_str(),pixel_format_.c_str(),w,h,fps_,encoder_.c_str(),bitrate_,bitrate_,std::max(1u,bitrate_/2),fps_,host_.c_str(),port_);STARTUPINFOA si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=read_pipe;si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);PROCESS_INFORMATION pi{};BOOL ok=CreateProcessA(nullptr,cmd,nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);CloseHandle(read_pipe);if(!ok){CloseHandle(pipe_);pipe_=INVALID_HANDLE_VALUE;encoder_failed_=true;debugf("failed to launch FFmpeg '%s': error %lu",ffmpeg_.c_str(),GetLastError());return false;}CloseHandle(pi.hThread);process_=pi.hProcess;encoder_failed_=false;debugf("FFmpeg started: pid=%lu stream=%ux%u",pi.dwProcessId,w,h);return true;}
void SvrtDirectMode::CloseEncoder(){if(pipe_!=INVALID_HANDLE_VALUE){CloseHandle(pipe_);pipe_=INVALID_HANDLE_VALUE;}if(process_){WaitForSingleObject(process_,2000);CloseHandle(process_);process_=nullptr;}}
void SvrtDirectMode::EncoderThread(){while(running_){std::unique_lock<std::mutex> l(mutex_);ready_.wait(l,[this]{return !running_||latest_virtual_handle_.load()!=0||std::any_of(slots_.begin(),slots_.end(),[](const Slot&s){return s.pending;});});if(!running_)break;if(!receiver_available_){latest_virtual_handle_=0;for(auto &slot:slots_)slot.pending=false;virtual_textures_.clear();l.unlock();CloseEncoder();l.lock();continue;}const uintptr_t virtual_handle=latest_virtual_handle_.exchange(0);if(virtual_handle){ComPtr<ID3D11Texture2D> source;auto cached=virtual_textures_.find(virtual_handle);if(cached!=virtual_textures_.end())source=cached->second;l.unlock();if(!source&&SUCCEEDED(device_->OpenSharedResource(reinterpret_cast<HANDLE>(virtual_handle),IID_PPV_ARGS(&source)))){l.lock();if(virtual_textures_.size()>=4)virtual_textures_.clear();virtual_textures_[virtual_handle]=source;l.unlock();}if(source){D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);std::lock_guard<std::mutex> dl(d3d_mutex_);l.lock();if(d.SampleDesc.Count==1&&EnsureVirtualSlots(d.Width,d.Height,d.Format)){auto free_slot=std::find_if(slots_.begin(),slots_.end(),[](const Slot&s){return !s.pending;});if(free_slot!=slots_.end()){context_->CopyResource(free_slot->staging.Get(),source.Get());context_->Flush();free_slot->pending=true;free_slot->sequence=++sequence_;}}l.unlock();}l.lock();}auto it=std::find_if(slots_.begin(),slots_.end(),[](const Slot&s){return s.pending;});if(it==slots_.end())continue;Slot *slot=&*it;unsigned w=width_,h=height_;uint64_t frame=slot->sequence;l.unlock();D3D11_MAPPED_SUBRESOURCE map{};bool ok=false;bool gpu_busy=false;
  // The immediate context must be serialized, but the CPU copy itself does
  // not.  Keeping this lock during a full stereo-frame memcpy made Present()
  // wait behind the encoder worker and produced visible compositor hitches.
  {
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    if(process_&&WaitForSingleObject(process_,0)==WAIT_OBJECT_0)encoder_failed_=true;
    const HRESULT mapped=encoder_failed_?E_FAIL:context_->Map(slot->staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
    gpu_busy=mapped==DXGI_ERROR_WAS_STILL_DRAWING;
    ok=SUCCEEDED(mapped);
  }
  if(gpu_busy){Sleep(1);continue;}
  if(ok){
    for(unsigned y=0;y<h;y++)std::memcpy(frame_.data()+(size_t)y*w*4,(uint8_t*)map.pData+(size_t)y*map.RowPitch,(size_t)w*4);
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    context_->Unmap(slot->staging.Get(),0);
  }
  HANDLE pipe=pipe_;if(ok&&pipe!=INVALID_HANDLE_VALUE){for(unsigned y=0;y<h&&ok;y++){DWORD n=0;ok=WriteFile(pipe,frame_.data()+(size_t)y*w*4,w*4,&n,nullptr)&&n==w*4;}}
  l.lock();slot->pending=false;
  if(ok&&(frame==1||frame%fps_==0))
    debugf("encoded frame=%llu size=%ux%u",(unsigned long long)frame,w,h);
  if(!ok&&running_&&!gpu_busy){const bool first_failure=!encoder_failed_.exchange(true);l.unlock();if(first_failure)debug("encoder pipe write failed; pausing before retry");CloseEncoder();l.lock();slots_.clear();l.unlock();Sleep(1000);if(running_&&receiver_available_)encoder_failed_=false;l.lock();}}}
